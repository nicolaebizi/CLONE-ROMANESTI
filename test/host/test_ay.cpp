#include "unity.h"
#include <pico/stdlib.h>
#include <new>
#include "ZxSpectrumAy.h"

void setUp(void) {}
void tearDown(void) {}

// The emulator's AY is a static, so its memory starts zeroed.
// Reproduce that, then sample in envelope mode before register 13 has ever been written.
void test_envelope_before_shape_written(void) {
  alignas(ZxSpectrumAy) static uint8_t mem[sizeof(ZxSpectrumAy)];
  memset(mem, 0, sizeof(mem));
  ZxSpectrumAy* ay = new (mem) ZxSpectrumAy();
  ay->writeCtrl(7);
  ay->writeData(0x3e); // tone A on, everything else off
  ay->writeCtrl(8);
  ay->writeData(0x10); // channel A volume from the envelope
  uint32_t vA, vB, vC;
  ay->vol(vA, vB, vC);
  ay->step(1000);
  ay->vol(vA, vB, vC);
  TEST_ASSERT_EQUAL(0, vB);
  TEST_ASSERT_EQUAL(0, vC);
  ay->~ZxSpectrumAy();
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_envelope_before_shape_written);
  return UNITY_END();
}
