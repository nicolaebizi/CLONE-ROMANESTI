#include "unity.h"
#include <vector>
#include "MemInputStream.h"
#include "PulseProcBitStream.h"
#include "PulseProcSymdefs.h"
#include "PulseProcTzxIndex.h"
#include "PulseProcChain.h"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t TZX_HEADER[] = { 'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1a, 0x01, 0x14 };

// Advance a pulse processor until it stops producing pulses, giving up after max steps
static int32_t run(PulseProc* pp, InputStream* is, uint32_t max, uint32_t* steps) {
  bool state = false;
  PulseProc* top = pp;
  int32_t r = 0;
  for (*steps = 0; *steps < max; ++*steps) {
    r = pp->advance(is, &state, &top);
    if (r < 0) break;
  }
  return r;
}

// ID 15 with "used bits in last byte" of 0 used to spin forever inside advance()
void test_bit_stream_zero_used_bits_terminates(void) {
  const uint8_t data[] = { 0x0f };
  MemInputStream is(data, sizeof(data));
  PulseProcBitStream bs;
  bs.init(0, 1, 10, 0);
  uint32_t steps;
  TEST_ASSERT_EQUAL(PP_COMPLETE, run(&bs, &is, 100, &steps));
  TEST_ASSERT_LESS_THAN(100, steps);
}

void test_bit_stream_partial_last_byte(void) {
  // 4 used bits of 0b1010.... gives 4 pulses of 1 bit each
  const uint8_t data[] = { 0xa0 };
  MemInputStream is(data, sizeof(data));
  PulseProcBitStream bs;
  bs.init(0, 1, 10, 4);
  uint32_t steps;
  TEST_ASSERT_EQUAL(PP_COMPLETE, run(&bs, &is, 100, &steps));
  TEST_ASSERT_EQUAL(4, steps);
}

void test_symdefs_reads_table(void) {
  // 2 symbols, each a type byte and 2 pulse words
  const uint8_t data[] = { 0, 1, 0, 2, 0, 3, 3, 0, 4, 0 };
  MemInputStream is(data, sizeof(data));
  std::vector<uint16_t> sd;
  PulseProcSymdefs symdefs(&sd);
  symdefs.init(0, 2, 2);
  bool state;
  PulseProc* top = &symdefs;
  TEST_ASSERT_EQUAL(PP_COMPLETE, symdefs.advance(&is, &state, &top));
  TEST_ASSERT_EQUAL(6, sd.size());
  TEST_ASSERT_EQUAL(2, symdefs.at(0, 2));
  TEST_ASSERT_EQUAL(4, symdefs.at(1, 2));
  // Out of range lookups are safe
  TEST_ASSERT_EQUAL(0, symdefs.at(2, 0));
}

// NPP=255, ASP=0 (256) asks for a 128KB table; this must be refused before allocating
void test_symdefs_rejects_huge_table(void) {
  const uint8_t data[] = { 0 };
  MemInputStream is(data, sizeof(data));
  std::vector<uint16_t> sd;
  PulseProcSymdefs symdefs(&sd);
  symdefs.init(0, 255, 0);
  bool state;
  PulseProc* top = &symdefs;
  TEST_ASSERT_EQUAL(PP_ERROR, symdefs.advance(&is, &state, &top));
  TEST_ASSERT_EQUAL(0, sd.capacity());
}

static int32_t indexBlocks(uint32_t n, std::vector<uint32_t>* bi) {
  // ID 22 (group end) has no body, so each byte is a whole block
  std::vector<uint8_t> data(n, 0x22);
  MemInputStream is(data.data(), data.size());
  PulseProcTzxIndex index;
  index.init(0, bi);
  bool state;
  PulseProc* top = &index;
  return index.advance(&is, &state, &top);
}

void test_tzx_index_small(void) {
  std::vector<uint32_t> bi;
  TEST_ASSERT_EQUAL(PP_COMPLETE, indexBlocks(10, &bi));
  TEST_ASSERT_EQUAL(10, bi.size());
}

void test_tzx_index_rejects_too_many_blocks(void) {
  std::vector<uint32_t> bi;
  TEST_ASSERT_EQUAL(PP_ERROR, indexBlocks(PULSE_PROC_TZX_MAX_BLOCKS + 1, &bi));
  TEST_ASSERT_LESS_OR_EQUAL(PULSE_PROC_TZX_MAX_BLOCKS, bi.size());
}

// A jump back to a block that takes no time used to hang the emulator
void test_chain_stops_on_jump_loop(void) {
  std::vector<uint8_t> data(TZX_HEADER, TZX_HEADER + sizeof(TZX_HEADER));
  const uint8_t blocks[] = {
    0x21, 0x01, 'A',  // ID 21 group start "A"
    0x23, 0xff, 0xff, // ID 23 jump -1
  };
  data.insert(data.end(), blocks, blocks + sizeof(blocks));
  MemInputStream is(data.data(), data.size());
  static PulseProcChain chain;
  chain.loadTzx(&is, 3500, true);
  bool state = false;
  chain.advance(1, &state);
  TEST_ASSERT_TRUE(chain.end());
}

void test_chain_plays_simple_tzx(void) {
  std::vector<uint8_t> data(TZX_HEADER, TZX_HEADER + sizeof(TZX_HEADER));
  const uint8_t blocks[] = {
    0x12, 0x10, 0x00, 0x03, 0x00, // ID 12 pure tone, 3 pulses of 16 t-states
  };
  data.insert(data.end(), blocks, blocks + sizeof(blocks));
  MemInputStream is(data.data(), data.size());
  static PulseProcChain chain;
  chain.loadTzx(&is, 3500, true);
  bool state = false;
  uint32_t edges = 0;
  for (uint32_t t = 0; t < 64 && !chain.end(); ++t) {
    const bool before = state;
    chain.advance(1, &state);
    if (state != before) ++edges;
  }
  TEST_ASSERT_GREATER_OR_EQUAL(2, edges);
}

// Play a tape to the end, returning the time between each change of signal level
static std::vector<uint32_t> edgeIntervals(PulseProcChain* chain, uint32_t maxTStates) {
  std::vector<uint32_t> intervals;
  bool state = false;
  uint32_t last = 0;
  for (uint32_t t = 1; t <= maxTStates && !chain->end(); ++t) {
    const bool before = state;
    chain->advance(1, &state);
    if (state != before) {
      intervals.push_back(t - last);
      last = t;
    }
  }
  return intervals;
}

static std::vector<uint8_t> tzx(const std::vector<uint8_t>& blocks) {
  std::vector<uint8_t> data(TZX_HEADER, TZX_HEADER + sizeof(TZX_HEADER));
  data.insert(data.end(), blocks.begin(), blocks.end());
  return data;
}

// Call targets are relative to the call block, so the call at block 1 with offset +2 plays block 3
void test_chain_call_sequence_is_relative(void) {
  const std::vector<uint8_t> data = tzx({
    0x22,                         // 0: ID 22 group end
    0x26, 0x01, 0x00, 0x02, 0x00, // 1: ID 26 call 1 block, offset +2
    0x12, 0x64, 0x00, 0x02, 0x00, // 2: ID 12 tone, 2 pulses of 100
    0x12, 0x32, 0x00, 0x03, 0x00, // 3: ID 12 tone, 3 pulses of 50
    0x27,                         // 4: ID 27 return
  });
  MemInputStream is(data.data(), data.size());
  static PulseProcChain chain;
  chain.loadTzx(&is, 3500, true);
  const std::vector<uint32_t> intervals = edgeIntervals(&chain, 5000000);
  TEST_ASSERT_TRUE(chain.end());
  // Called block 3, returned to block 2, then played on through block 3 and the stray return
  const uint32_t expected[] = { 50, 50, 50, 100, 100, 50, 50, 50 };
  TEST_ASSERT_EQUAL(sizeof(expected) / sizeof(expected[0]), intervals.size());
  TEST_ASSERT_EQUAL_UINT32_ARRAY(expected, intervals.data(), intervals.size());
}

// A call sequence left part way through by an eject must not affect the next tape
void test_chain_call_state_reset_between_tapes(void) {
  static PulseProcChain chain;

  const std::vector<uint8_t> tape1 = tzx({
    0x26, 0x02, 0x00, 0x01, 0x00, 0x01, 0x00, // 0: ID 26 call 2 blocks, offsets +1, +1
    0x12, 0x64, 0x00, 0x10, 0x00,             // 1: ID 12 tone, 16 pulses of 100
  });
  MemInputStream is1(tape1.data(), tape1.size());
  chain.loadTzx(&is1, 3500, true);
  edgeIntervals(&chain, 250);
  TEST_ASSERT_FALSE(chain.end());
  chain.reset();

  const std::vector<uint8_t> tape2 = tzx({
    0x27,                         // 0: ID 27 return, with no call on this tape
    0x12, 0x46, 0x00, 0x02, 0x00, // 1: ID 12 tone, 2 pulses of 70
  });
  MemInputStream is2(tape2.data(), tape2.size());
  chain.loadTzx(&is2, 3500, true);
  const std::vector<uint32_t> intervals = edgeIntervals(&chain, 5000000);
  TEST_ASSERT_TRUE(chain.end());
  const uint32_t expected[] = { 70, 70 };
  TEST_ASSERT_EQUAL(2, intervals.size());
  TEST_ASSERT_EQUAL_UINT32_ARRAY(expected, intervals.data(), intervals.size());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_bit_stream_zero_used_bits_terminates);
  RUN_TEST(test_bit_stream_partial_last_byte);
  RUN_TEST(test_symdefs_reads_table);
  RUN_TEST(test_symdefs_rejects_huge_table);
  RUN_TEST(test_tzx_index_small);
  RUN_TEST(test_tzx_index_rejects_too_many_blocks);
  RUN_TEST(test_chain_stops_on_jump_loop);
  RUN_TEST(test_chain_plays_simple_tzx);
  RUN_TEST(test_chain_call_sequence_is_relative);
  RUN_TEST(test_chain_call_state_reset_between_tapes);
  return UNITY_END();
}
