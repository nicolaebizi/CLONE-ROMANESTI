// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#include "ZxSpectrum.h"
#include "basic.h"
#include "turbo_basic.h"
#include "cobra_unified_boot.h"
#include "cobra_system_rom.h"
#include "cubz_rom.h"
#include "hc2000_if1_v2_patch.h"
#if PICO_RP2350
// TIM-S Plus boot-only (RP2350 only): 2 KiB boot EPROM mirrored x8 into the
// 16K low bank.
#include "tims_boot_rom.h"
// HC88 phase 1 (RP2350 only): the 16K motherboard ROM dump.
#include "hc88_rom.h"
#endif
#ifdef ZX_PLUS3_ROM_AVAILABLE
#include "plus3_roms.h"
#endif

#include <string.h>
#include <pico/printf.h>
#include <pico/stdlib.h>
#include "SizingOutputStream.h"

#ifndef ZX_SPECTRUM_INT_SRC 
#define ZX_SPECTRUM_INT_SRC SyncToCpu
#endif

// HC2000: decode the compact base64/RLE patch and apply it over the embedded
// v1 IF1 ROM copied into the shadow RAM page, producing the canonical v2.
namespace {
static uint8_t if1V2PatchBytes[927];

static int base64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

static void loadIf1V2(uint8_t *destination) {
  memcpy(destination, hc2000_if1_rom, 0x4000);

  uint32_t accumulator = 0;
  uint8_t bits = 0;
  uint32_t output = 0;
  for (const char *p = hc2000_if1_v2_patch_rle_b64; *p; p++) {
    const int value = base64Value(*p);
    if (value < 0) break;
    accumulator = (accumulator << 6) | (uint32_t)value;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (output < sizeof(if1V2PatchBytes))
        if1V2PatchBytes[output++] = (uint8_t)(accumulator >> bits);
      accumulator &= bits ? ((1u << bits) - 1u) : 0u;
    }
  }

  uint32_t runOffset = 0;
  uint16_t runRemaining = 0;
  uint8_t runValue = 0;
  const uint32_t rangeCount = sizeof(hc2000_if1_v2_patch_ranges) / sizeof(hc2000_if1_v2_patch_ranges[0]);
  for (uint32_t r = 0; r < rangeCount; r++) {
    const uint16_t start = hc2000_if1_v2_patch_ranges[r][0];
    const uint16_t length = hc2000_if1_v2_patch_ranges[r][1];
    for (uint16_t i = 0; i < length; i++) {
      if (runRemaining == 0) {
        if (runOffset + 3 > output) return;
        runRemaining = (uint16_t)(if1V2PatchBytes[runOffset] |
          (if1V2PatchBytes[runOffset + 1] << 8));
        runValue = if1V2PatchBytes[runOffset + 2];
        runOffset += 3;
      }
      destination[start + i] = runValue;
      runRemaining--;
    }
  }
}
}

// CoBra: the Z80 leaves reset one D02 diode drop before NPOR goes inactive
// (a few milliseconds).  Until then the 8255 is held in reset and PO is
// preset; the boot ROM waits with its DELAY loop (~49ms) before using it.
#define COBRA_POR_TSTATES (5 * 3500)

ZxSpectrum::ZxSpectrum(
    ZxSpectrumKeyboard *keyboard1,
    ZxSpectrumKeyboard *keyboard2,
    ZxSpectrumJoystick *joystick,
    ZxSpectrumMouse *mouse
) :
  _moderate(9),
  _keyboard1(keyboard1),
  _keyboard2(keyboard2),
  _joystick(joystick),
  _mouse(mouse),
  _borderColour(7),
  _port254(0),
  _portMem(0),
  _plus3Latch1ffd(0),
  _feReg(0),
  _feCfg(0),
  _hcLatch7e(0),
  _hcCpm(false),
  _hcIf1Shadow(false),
  _cobraConfig(Cobra80K80Col),
  _cobraPO(true),
  _cobraLO6(true),
  _cobra80(false),
  _cobraRoBasic(true),
  _cobraPor(0),
  _cobraPpiCtrl(0x9B),
  _cobraPortC(0),
#if PICO_RP2350
  _timsPaging(0),
  _timsSecondary(0),
  _timsPc(0),
  _timsPa(0),
  _timsPb(0),
  _timsControl(0x9b),
  _timsScroll(0),
  _wrVram(0), // diag counter starts at 0 once per power-on
  // HC88 (phase 1): latch at 0 (fuse hc88_reset: /LOAD asserted, screen 0,
  // 256-pixel mode) and the IF1 shadow released.
  _hc88Latch(0),
  _hc88If1Paged(false),
#endif
  _ear(false),
  _earInvert(0),
  _earDc(0),
  _intSource(ZX_SPECTRUM_INT_SRC),
  _mute(false),
  _buzzer(0),
  _buzzerl(0),
  _bzf(0),
  _sl(0),
  _slc(0)
{ 
  // Initialise to the null audio driver
  setAudioDriver(&_zx_spectrum_audio_drivers[zx_spectrum_audio_driver_null_index]);
  z80Power(true);
  _Z80.context = this;
  _Z80.options = Z80_MODEL_ZILOG_NMOS;
  _Z80.read = readByte;
  _Z80.fetch = readByte;
  _Z80.fetch_opcode = readByte;
  _Z80.write = writeByte;
  _Z80.in = readIO;
  _Z80.out = writeIO;
  _Z80.inta = readInt;
  for (uint8_t d = 0; d < CobraFdc::DRIVES; ++d) _cubzFdc.attach(d, &_cubzDisk[d]);
  reset(ZxSpectrum128k);
}

void ZxSpectrum::setAudioDriver(zx_spectrum_audio_driver_t* audio_driver) {
  _audio_driver = audio_driver;
  // Set the audio handler
  _audio_handler = audio_driver->handler;
  _audio_ready = audio_driver->ready;
  uint32_t hz = audio_driver->freq();
  // Time for a single audio out sample in 32nds of a micro second
  // 90%: const int32_t u32pas = (90UL * (1000000UL << 5UL)) / (hz * 100UL);
  u32pas = ((1000000 << 5) / hz);
  // Time for a single audio in sample in 32nds of a micro second
  u32pes = ((1000000 << 5) / 1000000);
}

void ZxSpectrum::transmute(ZxSpectrumType type) {
  _type = type;
  if (type == ZxSpectrumCobra) {
    _Z80.fetch_opcode = cobraFetchOpcode;
    _Z80.reti = cobraReti;
  }
  else if (type == ZxSpectrumHc2000) {
    // HC2000: opcode fetches drive the Interface I shadow in/out events.
    _Z80.fetch_opcode = hcFetchOpcode;
    _Z80.reti = 0;
  }
#if PICO_RP2350
  else if (type == ZxSpectrumHc88) {
    // HC88: opcode fetches arm / release the IF1 shadow-RAM latch
    // (fuse machines/hc88.c:114-124, decoded M1 ranges).
    _Z80.fetch_opcode = hcFetchOpcode;
    _Z80.reti = 0;
  }
#endif
  else {
    _Z80.fetch_opcode = readByte;
    _Z80.reti = 0;
  }
  switch (type) {
    case ZxSpectrumCobra:
    case ZxSpectrumCubz:
      _portMem = 0x20;
      break;
    case ZxSpectrum48k:
      setPageaddr(0, (uint8_t*)basic);
      _portMem = 0x20;
      break;
    case ZxSpectrumTurbo:
      setPageaddr(0, (uint8_t*)basic_turbo);
      _portMem = 0x20;
      break;
    case ZxSpectrumHc2000:
      setPageaddr(0, (uint8_t*)hc2000_main_rom);
      _portMem = 0x20;
      _hcLatch7e = 0;
      _hcCpm = false;
      _hcIf1Shadow = false;
      break;
#if PICO_RP2350
    case ZxSpectrumTimsPlus:
      // The 2K boot EPROM mirrored into the 16K low bank; register-panel
      // latches take their reset state (fuse tims_reset: all 0, control 0x9b).
      setPageaddr(0, (uint8_t*)tims_boot_rom);
      _portMem = 0x20;
      _timsPaging = _timsSecondary = _timsPc = _timsPa = _timsPb = _timsScroll = 0;
      _timsControl = 0x9b;
      break;
    case ZxSpectrumHc88:
      // HC88 (fuse hc88_reset): latch = 0 (b0 clear -> the 16K motherboard
      // ROM visible at Q0 for reads, MOD1/MOD0 clear/LOCK off, hires off),
      // IF1 shadow released. The reset path calls applyHc88Map() after the
      // standard reset page table fill.
      setPageaddr(0, (uint8_t*)hc88_rom);
      _portMem = 0x20;
      _hc88Latch = 0;
      _hc88If1Paged = false;
      break;
#endif
    case ZxSpectrum128k:
    case ZxSpectrumPlus3:
    default:
      setPageaddr(0, (uint8_t*)zx_128k_rom_1);
      _portMem = 0;
      _plus3Latch1ffd = 0;
      break;
  }
}

// Turbo Spectrum FE_CFG[6:5] -> memory map (fig. 11)
// 00: EEPROM mode    $0000=ROM, $4000=RAM0, $8000=RAM1, $C000=RAM5
// 20: 64K DRAM mode  $0000=RAM6, $4000=RAM0, $8000=RAM1, $C000=RAM5
// 40/60: Spectrum    $0000=RAM1(RO), $4000=RAM5, $8000=RAM6, $C000=RAM0
void ZxSpectrum::applyTurboMap() {
  const uint8_t cfg = _feCfg & 0x60;
  if (cfg == 0x20) {
    setPageaddr(0, (uint8_t*)&_RAM[6]);
    setPageaddr(1, (uint8_t*)&_RAM[0]);
    setPageaddr(2, (uint8_t*)&_RAM[1]);
    setPageaddr(3, (uint8_t*)&_RAM[5]);
  }
  else if (cfg & 0x40) {
    setPageaddr(0, (uint8_t*)&_RAM[1]);
    setPageaddr(1, (uint8_t*)&_RAM[5]);
    setPageaddr(2, (uint8_t*)&_RAM[6]);
    setPageaddr(3, (uint8_t*)&_RAM[0]);
  }
  else {
    setPageaddr(0, (uint8_t*)basic_turbo);
    setPageaddr(1, (uint8_t*)&_RAM[0]);
    setPageaddr(2, (uint8_t*)&_RAM[1]);
    setPageaddr(3, (uint8_t*)&_RAM[5]);
  }
}

// HC2000: with D1 clear, D0 selects the 16K EPROM half at $0000-$3FFF.
// With D1 set, that EPROM's upper 8K is overlaid at $E000-$FFFF and the
// underlying 16K RAM is exposed at $0000-$3FFF. _RAM[1] backs that hidden
// low RAM; the other base RAM pages retain the reset layout.
void ZxSpectrum::applyHc2000Map() {
  if (_hcLatch7e & 2)
    setPageaddr(0, (uint8_t*)&_RAM[1]);
  else
    setPageaddr(0, (uint8_t*)(hc2000_main_rom + ((_hcLatch7e & 1) << 14)));
}

#if PICO_RP2350
// TIM-S Plus memory map (fuse machines/tims_plus.c:37-63, followed
// literally):
//   (a) secondary bit0 set: TC/128K-style rows
//       {0,1,2,3},{4,5,6,7},{4,5,6,3},{4,7,6,3} (secondary>>1 selects);
//   (b) PC bit4 set: CP/M Vol-III pages
//       [8+((paging>>4)&1)+((secondary>>1)&2), 5, 2,
//        ((pc&4)?8:0)+((paging&4)?4:0)+(paging&3); >=12 reads as the VRAM],
//       plus the separate VRAM 8K half overlaid read-only at $4000-$5FFF
//       when pc&4 && paging&4 (write map stays on page 5, see writeByte);
//   (c) otherwise: fixed pages 11,9,10,8 with the Q1/Q2 window ($4000 when
//       paging bit4, else $8000) showing system page 0-7, system page 8-11
//       (paging bit5 + pc bit1, no paging bit2) or the VRAM 8K half
//       (paging bit5 + !pc bit1 + paging bit2), the VRAM half shown
//       mirrored into both window halves (paging bit3 selects).
//   The low ROM (2K boot EPROM mirrored) replaces the $0000-$3FFF READ map
//   whenever PC0 is clear; its write map stays on the underlying system
//   bank (locked for banks 8-11 unless PC1).
void ZxSpectrum::applyTimsMap() {
  static const uint8_t extra[4][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {4, 5, 6, 3}, {4, 7, 6, 3}};
  uint8_t pages[4];
  int8_t videoWindow = -1;
  if (_timsSecondary & 1) {
    const uint8_t *row = extra[(_timsSecondary >> 1) & 3];
    for (uint8_t q = 0; q < 4; ++q) pages[q] = row[q];
  }
  else if (_timsPc & 16) {
    pages[0] = 8 + ((_timsPaging >> 4) & 1) + ((_timsSecondary >> 1) & 2);
    pages[1] = 5;
    pages[2] = 2;
    pages[3] = ((_timsPc & 4) ? 8 : 0) + ((_timsPaging & 4) ? 4 : 0) + (_timsPaging & 3);
  }
  else {
    // Volume III p.562: fixed CP/M system pages, selectable Q1/Q2 window.
    const uint8_t window = (_timsPaging & 16) ? 1 : 2;
    pages[0] = 11; pages[1] = 9; pages[2] = 10; pages[3] = 8;
    if (!(_timsPaging & 32) && !(_timsPc & 1)) pages[window] = _timsPaging & 7;
    else if ((_timsPaging & 32) && (_timsPc & 1) && !(_timsPaging & 4))
      pages[window] = 8 + (_timsPaging & 3);
    else if ((_timsPaging & 32) && !(_timsPc & 1) && (_timsPaging & 4)) {
      pages[window] = 12;
      videoWindow = window;
    }
  }
  for (uint8_t q = 0; q < 4; ++q) {
    _timsPages[q] = pages[q];
    setPageaddr(q, pages[q] >= 12 ? (uint8_t*)_timsVram : (uint8_t*)&_timsRAM[pages[q]]);
  }
  if (videoWindow >= 0) {
    // The same 8K half of the VRAM is mirrored into both window halves.
    const uint8_t *half = _timsVram + ((_timsPaging & 8) ? 0x2000 : 0);
    setPage8(videoWindow << 1, half);
    setPage8((videoWindow << 1) + 1, half);
  }
  else if ((_timsPc & 16) && !(_timsSecondary & 1) && (_timsPc & 4) && (_timsPaging & 4)) {
    // CP/M Vol-III: the $4000-$5FFF window reads the VRAM 8K half; the
    // write map stays on system page 5 (writeByte does the dual store).
    setPage8(2, _timsVram + ((_timsPaging & 8) ? 0x2000 : 0));
  }
  if (!(_timsPc & 1))
    setPageaddr(0, (uint8_t*)tims_boot_rom); // reads of $0000-$3FFF
}

// HC88 memory map (fuse machines/hc88.c:56-92, followed literally), called
// on every latch write and on every IF1 arming/unpage event:
//  1. Quadrants 0-3 default to the 64K main RAM banks 0-3.
//  2. MOD1 (b3): the separate 16K VRAM replaces every quadrant
//     (hc88.c:63-66 sheet 5); MOD0=0 (=b5 clear) otherwise selects the VRAM
//     only in Q1 (hc88.c:67-69).
//  3. IF1 shadow (U37A, hc88.c:70-72 sheet 5 MUXA14): when armed (see
//     readOpcode) with b0 set and MOD1 clear, the physical Q1 bank fills
//     the whole of Q0 for both reads and writes.
//  4. b0 clear: the motherboard 16K ROM overlays $0000-$3FFF for READS
//     only (memory_map_16k_read_write(...,1,0) at hc88.c:89-90); the
//     write map keeps pointing at main RAM bank 0, which writeByte
//     implements (LOCK inhibits, see writeByte). Shadow/ROM never overlap
//     (arming requires b0 set).
// The raster ignores the CPU map: the screen is always the VRAM
// (memory_current_screen = 4, hc88.c:48-49,73); bit 4 (hires) is stored
// only, no scld mode switch this phase.
void ZxSpectrum::applyHc88Map() {
  const bool mod1 = (_hc88Latch & 0x08) != 0;
  setPageaddr(1, (uint8_t*)&_RAM[1]);
  setPageaddr(2, (uint8_t*)&_RAM[2]);
  setPageaddr(3, (uint8_t*)&_RAM[3]);
  if (mod1) {
    setPageaddr(1, (uint8_t*)&_hc88Vram);
    setPageaddr(2, (uint8_t*)&_hc88Vram);
    setPageaddr(3, (uint8_t*)&_hc88Vram);
  }
  else if (!(_hc88Latch & 0x20)) {
    setPageaddr(1, (uint8_t*)&_hc88Vram); // MOD0=0: VRAM in Q1 only
  }
  if (!mod1 && (_hc88Latch & 0x01) && _hc88If1Paged) {
    setPageaddr(0, (uint8_t*)&_RAM[1]); // shadow IF1 fills Q0
  }
  else if (mod1) {
    setPageaddr(0, (uint8_t*)&_hc88Vram);
  }
  else if (!(_hc88Latch & 0x01)) {
    setPageaddr(0, (uint8_t*)hc88_rom); // b0 clear: ROM at Q0 (reads)
  }
  else {
    setPageaddr(0, (uint8_t*)&_RAM[0]); // b0 set: low RAM at Q0
  }
}
#endif

void ZxSpectrum::reset(ZxSpectrumType type)
{
  _type = type;
  z80Reset();
  _ta32 = 0;
  _te32 = 0;
  _eb = 0;
  _ebc = 0;
  _earInvert = 0;
  _earDc = 0;
  moderate(9);
  transmute(type);
  // HC2000: reset the i8272 controller state (mounted media is kept)
  _hcFdc.softReset();
  _hcFdc.setPlus3Mode(type == ZxSpectrumPlus3);
#if PICO_RP2350
  // TIM-S Plus: FDC soft reset (drive A/B media stays mounted across resets,
  // like HC2000); the motor line off follows the reset $1FFD = 0.
  _hcFdc.setTimsMode(type == ZxSpectrumTimsPlus);
  // HC88: FDC port map + idle state come up mastered-reset (command-ready,
  // RQM=1), per fuse hc88_fdc_reset(); mounted media stays; the $E7 control
  // latch is written by the HC88 BIOS via setHc88Latch().
  _hcFdc.setHc88Mode(type == ZxSpectrumHc88);
#endif
  setPageaddr(1, (uint8_t*)&_RAM[5]);
  setPageaddr(2, (uint8_t*)&_RAM[2]);
  setPageaddr(3, (uint8_t*)&_RAM[0]);
  _ear = false;
  _pulseChain.reset();
  memset(_RAM, 0, sizeof(_RAM));
#if PICO_RP2350
  // TIM S Plus: zero the 12 system banks and the video RAM on reset so the
  // boot ROM executes against a deterministic machine state.
  if (type == ZxSpectrumTimsPlus) {
    memset(_timsRAM, 0, sizeof(_timsRAM));
    memset(_timsVram, 0, sizeof(_timsVram));
  }
  // HC88: the shared base _RAM pool (including the HC88 main RAM in its
  // banks 0-3) is already memset above for every machine reset; zero the
  // separate 16K VRAM so the machine ROM executes against a deterministic
  // state.
  else if (type == ZxSpectrumHc88) {
    memset(_hc88Vram, 0, sizeof(_hc88Vram));
  }
#endif
  // HC2000: the Interface I shadow page gets the patched v2 ROM
  if (type == ZxSpectrumHc2000) loadIf1V2((uint8_t*)&_RAM[7]);
  // Turbo Spectrum: F5 config and its effective mapping reset to EEPROM mode
  _feReg = 0;
  _feCfg = 0;
  if (type == ZxSpectrumTurbo) applyTurboMap(); // EEPROM mode after RST
  else if (type == ZxSpectrumHc2000) applyHc2000Map(); // ROM half 0 after RST
  else if (type == ZxSpectrumPlus3) applyPlus3Map(); // Plus3 mode
#if PICO_RP2350
  else if (type == ZxSpectrumTimsPlus) applyTimsMap(); // boot map after RST
  else if (type == ZxSpectrumHc88) applyHc88Map(); // reset map: ROM at Q0, VRAM at Q1 (latch 0)
#endif
  _port254 = 0x20;  //0x30;
  _ay.reset();
  if (_keyboard1) _keyboard1->reset();
  if (_keyboard2) _keyboard2->reset();
  if (_mouse) _mouse->reset();
  _tu32 = time_us_32() << 5;
  _sl = 0; 
  _slc = 0;
  _fc = 0;
  _fcf = 0;
  _bzf = 0;
  _buzzer = 0;
  if (type == ZxSpectrumCobra) {
    // NPOR: both configuration flip-flops preset (startup map), the 8255
    // reset with all its ports as inputs, the floppy interface reset.
    _cobraPO = true;
    _cobraLO6 = true;
    _cobra80 = false;
    _cobraPpiCtrl = 0x9B;
    _cobraPortC = 0;
    _cobraPor = COBRA_POR_TSTATES;
    _cobraFloppy.reset();
    cobraPortChanged();
  }
  if (type == ZxSpectrumCubz) {
    // Reset: EPROMs enabled, PIO-B outputs high, 8257 idle, i8272 reset.
    _cubzRom = true;
    _cubzPioB = 0xFF;
    _cubzDmaAddr = _cubzDmaCount = 0;
    _cubzDmaAddrHi = _cubzDmaCountHi = _cubzDmaActive = _cubzDmaTc = false;
    _cubzScroll = 0;
    _cubzCsinch = 0;
    _cubzKbdChar = _cubzKbdPhase = 0;
    _cubzKbdBit = true;
    _cubzKbdHead = _cubzKbdTail = 0;
    _cubzFdc.reset();
    // The reset logic fetches the first opcode at $F800 (monitor entry); the
    // RAM jump keeps the destructive 64K memory test of the monitor working.
    _RAM[0][0] = 0xC3; _RAM[0][1] = 0x3F; _RAM[0][2] = 0xF8;
    cubzMap();
    Z80_PC(_Z80) = 0xF800;
    _port254 = 0;
    _borderColour = 0;
    moderate(13); // Z80 at 2.5MHz
  }
}

// CUB-Z memory: a flat 64K RAM (_RAM[0..3]); PIO-B bit 2 (+VDB) puts the
// separate 16K video RAM (_RAM[4]) at $8000-$BFFF; PIO-B bit 3 (/PROMOFF)
// high overlays the three 2716 EPROMs at $E800, $F000 and $F800 for reads.
void ZxSpectrum::cubzMap() {
  for (uint32_t i = 0; i < 4; ++i) setPageaddr(i, (uint8_t*)&_RAM[i]);
  if (_cubzPioB & 0x04) setPageaddr(2, (uint8_t*)&_RAM[4]);
  if (_cubzRom) {
    memcpy(_cubzTop, &_RAM[3][0x2000], 0x800);
    memcpy(_cubzTop + 0x800, cubz_rom, sizeof(cubz_rom));
    setPage8(7, _cubzTop);
  }
}

bool ZxSpectrum::cubzInsertDisk(uint8_t drive, CobraDiskMedia *media) {
  if (drive >= CobraFdc::DRIVES) return false;
  _cubzDisk[drive].close();
  const bool ok = media ? _cubzDisk[drive].open(media) : false;
  _cubzFdc.mediaChanged(drive);
  return ok;
}

void ZxSpectrum::cubzKey(uint8_t ascii) {
  const uint8_t next = (uint8_t)((_cubzKbdHead + 1) & 15);
  if (next == _cubzKbdTail) return; // queue full
  _cubzKbdQ[_cubzKbdHead] = ascii;
  _cubzKbdHead = next;
}

// CUB-Z ports (Fuse machines_periph.c cubz_io_ports):
//   $84/$85 8257 channel 2 address/count, $88 8257 mode (W) / status (R)
//   $90 i8272 MSR, $91 i8272 data, $B0/$B4 configuration switch (reads 00)
//   $C2 PIO-B, $F2 VDT-52S SCROLL latch, $F3 CSINCH latch
uint8_t ZxSpectrum::cubzReadIO(uint16_t address) {
  switch (address & 0xFF) {
    case 0xB0:
    case 0xB4:
      return 0x00;
    case 0x88: {
      const uint8_t st = _cubzDmaTc ? 0x04 : 0x00;
      _cubzDmaTc = false;
      return st;
    }
    case 0x90: {
      uint8_t st = _cubzFdc.readStatus();
      // One 8257 channel 2 cycle per RQM poll.  The DMA addresses the main
      // RAM directly (no video RAM or EPROM overlay) and raises TC with the
      // last byte (count N-1 moves N bytes).
      if (_cubzDmaActive && (st & 0x80) && _cubzFdc.executing()) {
        const bool last = _cubzDmaCount == 0;
        if (last) _cubzFdc.terminalCount();
        if (st & 0x40) cubzPoke(_cubzDmaAddr, _cubzFdc.readData());
        else _cubzFdc.writeData(_RAM[_cubzDmaAddr >> 14][_cubzDmaAddr & 0x3FFF]);
        ++_cubzDmaAddr;
        if (last) {
          _cubzDmaActive = false;
          _cubzDmaTc = true;
        }
        else {
          --_cubzDmaCount;
        }
        st = _cubzFdc.readStatus();
        // While the DMA owns the data register RQM is not offered to the CPU.
        if (_cubzFdc.executing()) st &= 0x7F;
      }
      return st;
    }
    case 0x91:
      return _cubzFdc.readData();
    case 0xC2:
      // Bit 6 = VDT-52S serial KBDATA, bit 5 reads high.
      return (_cubzPioB & ~0x60) | 0x20 | (_cubzKbdBit ? 0x40 : 0x00);
    default:
      return 0xFF;
  }
}

void ZxSpectrum::cubzWriteIO(uint16_t address, uint8_t value) {
  switch (address & 0xFF) {
    case 0x84:
      if (!_cubzDmaAddrHi) _cubzDmaAddr = (_cubzDmaAddr & 0xFF00) | value;
      else _cubzDmaAddr = (_cubzDmaAddr & 0x00FF) | ((uint16_t)value << 8);
      _cubzDmaAddrHi = !_cubzDmaAddrHi;
      break;
    case 0x85:
      if (!_cubzDmaCountHi) _cubzDmaCount = (_cubzDmaCount & 0x3F00) | value;
      else _cubzDmaCount = (_cubzDmaCount & 0x00FF) | ((uint16_t)(value & 0x3F) << 8);
      _cubzDmaCountHi = !_cubzDmaCountHi;
      break;
    case 0x88:
      _cubzDmaActive = (value & 0x04) != 0;
      _cubzDmaTc = false;
      break;
    case 0x91:
      _cubzFdc.writeData(value);
      break;
    case 0xC2: {
      // Keyboard clock (bit 7) falling edge shifts the next serial bit:
      // a 0 start bit, then the 8 data bits LSB first.
      if ((_cubzPioB & 0x80) && !(value & 0x80)) {
        if (!_cubzKbdChar && _cubzKbdHead != _cubzKbdTail) {
          _cubzKbdChar = _cubzKbdQ[_cubzKbdTail];
          _cubzKbdTail = (uint8_t)((_cubzKbdTail + 1) & 15);
          _cubzKbdPhase = 0;
        }
        if (_cubzKbdChar) {
          _cubzKbdBit = _cubzKbdPhase == 0 ? false : ((_cubzKbdChar >> (_cubzKbdPhase - 1)) & 1);
          if (++_cubzKbdPhase > 8) { _cubzKbdChar = 0; _cubzKbdPhase = 0; }
        }
        else {
          _cubzKbdBit = true;
        }
      }
      const bool remap = ((_cubzPioB ^ value) & 0x0C) != 0;
      _cubzPioB = value;
      _cubzRom = (value & 0x08) != 0;
      if (remap) cubzMap();
      break;
    }
    case 0xF2:
      _cubzScroll = value;
      break;
    case 0xF3:
      _cubzCsinch = value;
      break;
    default:
      break;
  }
}

void ZxSpectrum::cobraResetBasic() {
  if (_type != ZxSpectrumCobra) return;
  // K1 pulls NRST only: the configuration flip-flops, the 8255 and the
  // 8272 keep their state.  In Spectrum mode (LO6=0) PO stays cleared, so
  // the CPU restarts the BASIC already copied into DRAM#0.
  z80Reset();
  _cobraFloppy.cpuReset();
  z80_int(&_Z80, _cobraFloppy.intLine());
}

// CoBra memory map (mainboard v.0.2 sheet 3: U36, U56 74LS42, JP9-JP15)
//
// 80K (JP10/JP11 1-2), AB15 = BA15 xor PO:
//   startup  PO=1 LO6=1  0000 BOOT     4000 SYSTEM   8000 DRAM#0  C000 O6 ? VRAM : DRAM#1
//   CP/M     PO=0 LO6=1  0000 DRAM#0   4000 O6 ? VRAM : DRAM#1  8000 DRAM#2  C000 DRAM#3
//   Spectrum      LO6=0  0000 DRAM#0 (R/O, JP9 1-2)  4000 DRAM#1, writes to 4000-5FFF
//                        also into VRAM  8000 DRAM#2  C000 DRAM#3
// Standard 64K (JP10/JP11 2-3), DRAM#1 is the VRAM, AB15 = BA15 xor LO6 and
// AB14 = BA14 xor (BA13 and LO6 and BA15):
//   startup/CP/M  0000 BOOT/DRAM#2  4000 SYSTEM/DRAM#3  8000 DRAM#0 low  A000 VRAM high
//                 C000 VRAM low  E000 DRAM#0 high
//   Spectrum      0000 DRAM#0 (R/O)  4000 VRAM  8000 DRAM#2  C000 DRAM#3
//
// SYSTEM EPROM U01 (27512): A14 = SO (PC7, JP4), A15 = TO (PC3, JP3).
void ZxSpectrum::cobraMap() {
  const uint8_t c = cobraPortC();
  // 80 chr/lin module: in CP/M an O5=1 sets the latch; it is cleared on
  // leaving the CP/M configuration.
  if (_cobraConfig != Cobra80K80Col || _cobraPO || !_cobraLO6) _cobra80 = false;
  else if (c & 0x20) _cobra80 = true;

  uint8_t *vram = (uint8_t*)&_RAM[5];
  uint8_t *dram0 = (uint8_t*)&_RAM[0];
  const uint8_t *sys = cobra_system_rom + (((c >> 3) & 1) << 15) + (((c >> 7) & 1) << 14);
  const uint8_t *page[4];
  uint8_t w[4];
  if (!_cobraLO6) {
    page[0] = dram0;
    w[0] = _cobraRoBasic ? CobraWrRom : CobraWrRam;
    page[1] = _cobraConfig == Cobra64K ? vram : (uint8_t*)&_RAM[1];
    w[1] = _cobraConfig == Cobra64K ? CobraWrRam : CobraWrVram;
    page[2] = (uint8_t*)&_RAM[2];
    w[2] = CobraWrRam;
    page[3] = (uint8_t*)&_RAM[3];
    w[3] = CobraWrRam;
  }
  else if (_cobraConfig == Cobra64K) {
    if (_cobraPO) {
      setPage8(0, cobra_unified_boot);          _cobraWr[0] = CobraWrRom;
      setPage8(1, cobra_unified_boot + 0x2000); _cobraWr[1] = CobraWrRom;
      setPage8(2, sys);                         _cobraWr[2] = CobraWrRom;
      setPage8(3, sys + 0x2000);                _cobraWr[3] = CobraWrRom;
    }
    else {
      setPage8(0, (uint8_t*)&_RAM[2]);          _cobraWr[0] = CobraWrRam;
      setPage8(1, (uint8_t*)&_RAM[2] + 0x2000); _cobraWr[1] = CobraWrRam;
      setPage8(2, (uint8_t*)&_RAM[3]);          _cobraWr[2] = CobraWrRam;
      setPage8(3, (uint8_t*)&_RAM[3] + 0x2000); _cobraWr[3] = CobraWrRam;
    }
    setPage8(4, dram0);          _cobraWr[4] = CobraWrRam;
    setPage8(5, vram + 0x2000);  _cobraWr[5] = CobraWrRam;
    setPage8(6, vram);           _cobraWr[6] = CobraWrRam;
    setPage8(7, dram0 + 0x2000); _cobraWr[7] = CobraWrRam;
    return;
  }
  else {
    uint8_t *slot = (c & 0x40) ? vram : (uint8_t*)&_RAM[1];
    if (_cobraPO) {
      page[0] = cobra_unified_boot; w[0] = CobraWrRom;
      page[1] = sys;                w[1] = CobraWrRom;
      page[2] = dram0;              w[2] = CobraWrRam;
      page[3] = slot;               w[3] = CobraWrRam;
    }
    else {
      page[0] = dram0;              w[0] = CobraWrRam;
      page[1] = slot;               w[1] = CobraWrRam;
      page[2] = (uint8_t*)&_RAM[2]; w[2] = CobraWrRam;
      page[3] = (uint8_t*)&_RAM[3]; w[3] = CobraWrRam;
    }
  }
  for (uint32_t i = 0; i < 4; ++i) {
    setPage8(i << 1, page[i]);
    setPage8((i << 1) + 1, page[i] + 0x2000);
    _cobraWr[i << 1] = w[i];
    // Only the lower 8K of the Spectrum screen page is written into VRAM.
    _cobraWr[(i << 1) + 1] = w[i] == CobraWrVram ? CobraWrRam : w[i];
  }
}

void ZxSpectrum::cobraPortChanged() {
  const uint8_t c = cobraPortC();
  // PC0-PC2 border, PC3 TO (tape out), PC4 LS (loudspeaker)
  _borderColour = c & 7;
  _port254 = c;
  cobraMap();
}

// U79 8255: /CS = A0 xnor A5, A0 = BA0, A1 = BNRD.
//   IN  A0=0 A5=1 (FE)  port A: K0-K5 keyboard, PA6 TI tape in, PA7 SI (RXD)
//   IN  A0=1 A5=0 (DF)  port B: Kempston joystick (pull-downs, 00 when idle)
//   OUT A0=0 A5=1 (FE)  port C
//   OUT A0=1 A5=0 (DF)  control word
uint8_t ZxSpectrum::cobraReadIO(uint16_t address) {
  uint8_t v = 0xFF;
  if (((address >> 5) ^ address) & 1) {
    if (!(address & 1)) {
      uint8_t kb = _keyboard1->read(address);
      if (_keyboard2) kb &= _keyboard2->read(address);
      v = (kb & 0x3F) | (((_ear ^ _earInvert) << 6) & (1 << 6));
    }
    else {
      v = _joystick ? _joystick->getKempston() : 0;
    }
  }
  if (CobraFloppy::ctcPort(address) || CobraFloppy::fdcPort(address)) {
    v &= _cobraFloppy.ioRead(address);
    z80_int(&_Z80, _cobraFloppy.intLine());
  }
  return v;
}

void ZxSpectrum::cobraWriteIO(uint16_t address, uint8_t value) {
  if ((((address >> 5) ^ address) & 1) && _cobraPor <= 0) {
    if (!(address & 1)) {
      _cobraPortC = value;
    }
    else if (value & 0x80) {
      // Mode set: the output latches are cleared.
      _cobraPpiCtrl = value;
      _cobraPortC = 0;
    }
    else {
      // Port C bit set/reset
      const uint8_t m = 1 << ((value >> 1) & 7);
      if (value & 1) _cobraPortC |= m; else _cobraPortC &= ~m;
    }
    cobraPortChanged();
  }
  if (CobraFloppy::ctcPort(address) || CobraFloppy::fdcPort(address)) {
#ifdef COBRA_FDC_TRACE
    printf("OUT (%02X),%02X PC=%04X\n", address & 0xFF, value, Z80_PC(_Z80));
#endif
    _cobraFloppy.ioWrite(address, value);
    z80_int(&_Z80, _cobraFloppy.intLine());
  }
}

// 0 - Z80 unmoderated
// 8 - Z80 at 4.0Mhz
// 9 - Z80 at 3.5Mhz
void ZxSpectrum::moderate(uint32_t mul) {
  if (mul == _moderate) return;
  if (mul) {
    _tu32 = time_us_32() << 5;
    _ta32 = 0;
    _te32 = 0;
    _ebc = 32;
    _eb = 0;
  }
  _moderate = mul;
}

void ZxSpectrum::toggleModerate() {
  switch(_moderate) {
    case 9: moderate(8); break;
    case 8: moderate(0); break;
    case 0: moderate(9); break;
    default: break;
  }
}

int32_t ZxSpectrum::writeZ80(OutputStream *os, int32_t version) {
  int32_t r = writeZ80Header(os, version);
  if (r < 0) {
    DBG_SPEC("Failed to write Z80 header\n");
    return r;
  }
  switch(version) {
    case 0: {
      r = writeZ80MemV0(os);
      break;
    }
    case 1: {
      r = writeZ80MemV1(os);
      break;
    }
    case 3: {
      r = writeZ80HeaderV3(os);
      if (r < 0) {
        DBG_SPEC("Failed to write Z80 header V3\n");
        return r;
      }
      r = writeZ80MemV2(os);
      if (r < 0) {
        DBG_SPEC("Failed to write Z80 memory V2\n");
        return r;
      }         
      break;
    }
    default: {
      DBG_SPEC("Writing Z80 version %ld is not implemented!\n", version);
      r = -2;
      break;
    }
  }
  os->close();
  return r;
}

int32_t ZxSpectrum::writeZ80MemV2(OutputStream *os) {
  for (uint32_t b = 0; b < 12; ++b) {
    const int32_t p = z80BlockToPage(b, _type);
    if (p >=0) {
      int32_t r = writeZ80MemV2(os, b, p);
      if (r < 0) {
        DBG_SPEC("Failed to write Z80 V2 block %ld\n", b);
        return r;
      }
    }
  }
  return 0;
}

int32_t ZxSpectrum::writeZ80MemV2(OutputStream *os, const uint8_t b, const uint8_t p) {
  uint8_t *blk = (uint8_t *)&_RAM[p];
  SizingOutputStream sos;
  int32_t r = writeZ80MemV2(&sos, blk);
  if (r < 0) return r;
  uint32_t l = sos.length();
  r = os->writeWord(l);
  if (r < 0) return r;
  r = os->writeByte(b);
  if (r < 0) return r;
  return writeZ80MemV2(os, blk);
}

int32_t ZxSpectrum::writeZ80MemV2(OutputStream *os, uint8_t *blk) {
  unsigned char buf[4];
  int32_t l = -1;
  int32_t c = 0;
  for (uint32_t i = 0; i < 0x4000; ++i) {
    int32_t b = blk[i];
    if (b == l) {
      c++;
      if (c == 0xff || i == 0x3fff || l == 0xed) {
        buf[0] = 0xed;
        buf[1] = 0xed;
        buf[2] = c;
        buf[3] = l;
        int32_t r = os->write(buf, 4);
        if (r < 0) return r;
        l = -1;
        c = 0;
      }
    }
    else {
      if (c > 4) {
        buf[0] = 0xed;
        buf[1] = 0xed;
        buf[2] = c;
        buf[3] = l;
        int32_t r = os->write(buf, 4);
        if (r < 0) return r;
      }
      else if (c > 0) {
        buf[0] = l;
        buf[1] = l;
        buf[2] = l;
        buf[3] = l;
        int32_t r = os->write(buf, c);
        if (r < 0) return r;
      }

      if (l == 0xed || i == 0x3fff) {
        int32_t r = os->writeByte(b);
        if (r < 0) return r;
        l = -1;
        c = 0;
      }
      else {
        l = b;
        c = 1;
      }
    }
  }
  return 0;
}

int32_t ZxSpectrum::writeZ80MemV1(OutputStream *os) {
  unsigned char buf[4];
  int32_t l = -1;
  int32_t c = 0;
  for (uint32_t i = 0x4000; i < 0x10000; ++i) {
    int32_t b = readByte(i);
    if (b == l) {
      c++;
      if (c == 0xff || i == 0xffff || l == 0xed) {
        buf[0] = 0xed;
        buf[1] = 0xed;
        buf[2] = c;
        buf[3] = l;
        int32_t r = os->write(buf, 4);
        if (r < 0) return r;
        l = -1;
        c = 0;
      }
    }
    else {
      if (c > 4) {
        buf[0] = 0xed;
        buf[1] = 0xed;
        buf[2] = c;
        buf[3] = l;
        int32_t r = os->write(buf, 4);
        if (r < 0) return r;
      }
      else if (c > 0) {
        buf[0] = l;
        buf[1] = l;
        buf[2] = l;
        buf[3] = l;
        int32_t r = os->write(buf, c);
        if (r < 0) return r;
      }

      if (l == 0xed || i == 0xffff) {
        int32_t r = os->writeByte(b);
        if (r < 0) return r;
        l = -1;
        c = 0;
      }
      else {
        l = b;
        c = 1;
      }
    }
  }
  return 0;
}

int32_t ZxSpectrum::writeZ80MemV0(OutputStream *os) {
  for (uint32_t i = 0x4000; i < 0x10000; ++i) {
    int32_t b = readByte(i);
    int32_t r = os->writeByte(b);
    if (r < 0) return r;
  }
  return 0;
}

int32_t ZxSpectrum::writeZ80Header(
  OutputStream *os,
  int32_t version
) {
  bool samRom = false; // TODO What is this ! ??
  bool compresed = version == 1;
  unsigned char buf[30];

  buf[0] = Z80_A(_Z80);
  buf[1] = Z80_F(_Z80);
  buf[2] = Z80_C(_Z80);
  buf[3] = Z80_B(_Z80);
  buf[4] = Z80_L(_Z80);
  buf[5] = Z80_H(_Z80);
  buf[6] = version > 1 ? 0 : Z80_PCL(_Z80);
  buf[7] = version > 1 ? 0 : Z80_PCH(_Z80);
  buf[8] = Z80_SPL(_Z80);
  buf[9] = Z80_SPH(_Z80);
  buf[10] = _Z80.i;
  buf[11] = _Z80.r;
  buf[12] = (
    (_Z80.r >> 7) |
    (_borderColour << 1) |
    ((samRom ? 1 : 0) << 4) |
    ((compresed ? 1 : 0) << 5)
  );
  buf[13] = Z80_E(_Z80);
  buf[14] = Z80_D(_Z80);
  buf[15] = Z80_C_(_Z80);
  buf[16] = Z80_B_(_Z80);
  buf[17] = Z80_E_(_Z80);
  buf[18] = Z80_D_(_Z80);
  buf[19] = Z80_L_(_Z80);
  buf[20] = Z80_H_(_Z80);
  buf[21] = Z80_A_(_Z80);
  buf[22] = Z80_F_(_Z80);
  buf[23] = Z80_IYL(_Z80);
  buf[24] = Z80_IYH(_Z80);
  buf[25] = Z80_IXL(_Z80);
  buf[26] = Z80_IXH(_Z80);
  buf[27] = _Z80.iff1;
  buf[28] = _Z80.iff2;
  buf[29] = _Z80.im;

  return os->write(buf, 30);
}

int32_t ZxSpectrum::loadZ80Header(InputStream *is) {
/*
        Offset  Length  Description
        ---------------------------
        0       1       A register
        1       1       F register
        2       2       BC register pair (LSB, i.e. C, first)
        4       2       HL register pair
        6       2       Program counter
        8       2       Stack pointer
        10      1       Interrupt register
        11      1       Refresh register (Bit 7 is not significant!)
        12      1       Bit 0  : Bit 7 of the R-register
                        Bit 1-3: Border colour
                        Bit 4  : 1=Basic SamRom switched in
                        Bit 5  : 1=Block of data is compressed
                        Bit 6-7: No meaning
        13      2       DE register pair
        15      2       BC' register pair
        17      2       DE' register pair
        19      2       HL' register pair
        21      1       A' register
        22      1       F' register
        23      2       IY register (Again LSB first)
        25      2       IX register
        27      1       Interrupt flipflop, 0=DI, otherwise EI
        28      1       IFF2 (not particularly important...)
        29      1       Bit 0-1: Interrupt mode (0, 1 or 2)
                        Bit 2  : 1=Issue 2 emulation
                        Bit 3  : 1=Double interrupt frequency
                        Bit 4-5: 1=High video synchronisation
                                 3=Low video synchronisation
                                 0,2=Normal
                        Bit 6-7: 0=Cursor/Protek/AGF joystick
                                 1=Kempston joystick
                                 2=Sinclair 2 Left joystick (or user
                                   defined, for version 3 .z80 files)
                                 3=Sinclair 2 Right joystick
*/
  DBG_SPEC("Reading Z80 header\n");
  unsigned char buf[30];
  int32_t l = is->read(buf, sizeof(buf));
  if (l < 30) {
    DBG_SPEC("Failed to read Z80 header\n");
    return -2; // error
  }
  if (buf[12] == 255) buf[12] = 1;
  uint32_t pc = ((uint32_t)buf[6]) + (((uint32_t)buf[7]) << 8);
  bool compressed = (1 << 5) & buf[12];
  Z80_A(_Z80) = buf[0];
  Z80_F(_Z80) = buf[1];
  Z80_C(_Z80) = buf[2];
  Z80_B(_Z80) = buf[3];
  Z80_L(_Z80) = buf[4];
  Z80_H(_Z80) = buf[5];
  Z80_PCL(_Z80) = buf[6];
  Z80_PCH(_Z80) = buf[7];
  Z80_SPL(_Z80) = buf[8];
  Z80_SPH(_Z80) = buf[9];
  _Z80.i = buf[10];
  _Z80.r = (buf[11] & 0x7f) + ((buf[12] & 1) << 7);
  _borderColour = (buf[12] & (7 << 1)) >> 1;
  Z80_E(_Z80) = buf[13];
  Z80_D(_Z80) = buf[14];
  Z80_C_(_Z80) = buf[15];
  Z80_B_(_Z80) = buf[16];
  Z80_E_(_Z80) = buf[17];
  Z80_D_(_Z80) = buf[18];
  Z80_L_(_Z80) = buf[19];
  Z80_H_(_Z80) = buf[20];
  Z80_A_(_Z80) = buf[21];
  Z80_F_(_Z80) = buf[22];
  Z80_IYL(_Z80) = buf[23];
  Z80_IYH(_Z80) = buf[24];
  Z80_IXL(_Z80) = buf[25];
  Z80_IXH(_Z80) = buf[26];
  _Z80.iff1 = buf[27] ? 1 : 0;
  _Z80.iff2 = buf[28] ? 1 : 0;
  _Z80.im = buf[29] & 3;
  if (buf[12] & (1<<4)) DBG_SPEC("WARNING: SamRom enabled\n");
  DBG_SPEC("PC %04lX\n", pc);
  DBG_SPEC("IFF1 %02X\n", buf[27]);
  DBG_SPEC("IFF2 %02X\n", buf[28]);
  DBG_SPEC("IM %02X\n", buf[29] & 3);
  DBG_SPEC("Joystick %02X\n", buf[29] >> 6);
  DBG_SPEC("Applied Z80 header\n");

  Z80_MEMPTR(_Z80) = Z80_PC(_Z80);
  return pc == 0 ? 2 : compressed ? 1 : 0;
}

int32_t ZxSpectrum::writeZ80HeaderV3(OutputStream *os) {
  uint8_t buf[57];
  buf[30 - 30] = (sizeof(buf)-2) & 0xff;
  buf[31 - 30] = (sizeof(buf)-2) >> 8;
  buf[32 - 30] = Z80_PCL(_Z80);
  buf[33 - 30] = Z80_PCH(_Z80);
  Z80_MEMPTR(_Z80) = Z80_PC(_Z80);
  switch(_type) {
    case ZxSpectrum48k:
    case ZxSpectrumTurbo: {
      buf[34 - 30] = 0;
      buf[35 - 30] = 255;
      break;
    }
    case ZxSpectrum128k: {
      buf[34 - 30] = 4;
      buf[35 - 30] = _portMem;
      break;
    }
    default:
      DBG_SPEC("Invalid machine type to write to Z80 v3 header %d\n", _type);
      return -2;
  }
  buf[36 - 30] = 255;
  buf[37 - 30] = 0x04;
  buf[38 - 30] = _ay.readCtrl();
  for (uint8_t i = 0; i < 16; ++i) buf[39 - 30 + i] = _ay.readData(i);
  buf[55 - 30] = 0;
  buf[56 - 30] = 0;
  buf[57 - 30] = 0;
  buf[58 - 30] = 0;
  buf[59 - 30] = 0;
  buf[60 - 30] = 0;
  buf[61 - 30] = 0;
  buf[62 - 30] = 0;
  for (uint8_t i = 0; i < 10; ++i) buf[63 - 30 + i] = 0;
  for (uint8_t i = 0; i < 10; ++i) buf[73 - 30 + i] = 0;
  buf[84 - 30] = 0;
  buf[85 - 30] = 0;
  buf[86 - 30] = 255;
  return os->write(buf, sizeof(buf));
}

int32_t ZxSpectrum::loadZ80HeaderV2(InputStream *is, ZxSpectrumType *type) {
/*
        Offset  Length  Description
        ---------------------------
      * 30      2       Length of additional header block (see below)
      * 32      2       Program counter
      * 34      1       Hardware mode (see below)
      * 35      1       If in SamRam mode, bitwise state of 74ls259.
                        For example, bit 6=1 after an OUT 31,13 (=2*6+1)
                        If in 128 mode, contains last OUT to 0x7ffd
	                      If in Timex mode, contains last OUT to 0xf4
      * 36      1       Contains 0xff if Interface I rom paged
                        If in Timex mode, contains last OUT to 0xff
      * 37      1       Bit 0: 1 if R register emulation on
                        Bit 1: 1 if LDIR emulation on
                        Bit 2: AY sound in use, even on 48K machines
                        Bit 6: (if bit 2 set) Fuller Audio Box emulation
                        Bit 7: Modify hardware (see below)
      * 38      1       Last OUT to port 0xfffd (soundchip register number)
      * 39      16      Contents of the sound chip registers
        55      2       Low T state counter
        57      1       Hi T state counter
        58      1       Flag byte used by Spectator (QL spec. emulator)
                        Ignored by Z80 when loading, zero when saving
        59      1       0xff if MGT Rom paged
        60      1       0xff if Multiface Rom paged. Should always be 0.
        61      1       0xff if 0-8191 is ROM, 0 if RAM
        62      1       0xff if 8192-16383 is ROM, 0 if RAM
        63      10      5 x keyboard mappings for user defined joystick
        73      10      5 x ASCII word: keys corresponding to mappings above
        83      1       MGT type: 0=Disciple+Epson,1=Disciple+HP,16=Plus D
        84      1       Disciple inhibit button status: 0=out, 0ff=in
        85      1       Disciple inhibit flag: 0=rom pageable, 0ff=not
     ** 86      1       Last OUT to port 0x1ffd
*/
  uint8_t buf[57];
  const int32_t hl = is->readWord();
  // V2 headers are 23 bytes and V3 54 or 55; anything shorter leaves fields we read below unset
  if (hl < 23 || hl > 57) {
    DBG_SPEC("Invalud Z80 V2/V3 header length %ld\n", hl);
    return -2; // error
  }
  int32_t l = is->read(buf, hl);
  if (l < hl) {
    DBG_SPEC("Failed to read Z80 V2 header\n");
    return -2; // error
  }
  Z80_PCL(_Z80) = buf[0];
  Z80_PCH(_Z80) = buf[1];
  const int32_t v = (hl >=54) ? 3 : 2;
/*
        Value:          Meaning in v2           Meaning in v3
      -----------------------------------------------------
        0             48k                     48k
        1             48k + If.1              48k + If.1
        2             SamRam                  SamRam
        3             128k                    48k + M.G.T.
        4             128k + If.1             128k
        5             -                       128k + If.1
        6             -                       128k + M.G.T.
*/ 
  const int32_t hm = buf[34-32];
  *type = (hm == 0) || (hm == 1) || ((hm == 3) && (v == 3)) ? ZxSpectrum48k : ZxSpectrum128k;
  DBG_SPEC("Found header for V%ld and hardware mode %ld is48k %s\n", v, hm, (*type == ZxSpectrum48k ? "yes" : "no"));
  transmute(type48(*type));
  _port254 = 0x20;  //0x30;
  if (*type == ZxSpectrum128k) {
    _portMem = buf[35-32];
    setPageaddr(3, (uint8_t*)&_RAM[_portMem & 7]);
    setPageaddr(0, (uint8_t*)((_portMem & 0x10) ? zx_128k_rom_2 : zx_128k_rom_1));
  }
  for (int32_t i = 0; i < 16; ++i) {
    _ay.writeCtrl(i);
    _ay.writeData(buf[i + 39-32]);
  }
  _ay.writeCtrl(buf[38-32]);
  return 0;
}

/*
        Block   In '48 mode     In '128 mode    In SamRam mode
        ------------------------------------------------------
         0      48K rom         rom (basic)     48K rom
         1      Interface I, Disciple or Plus D rom, according to setting
         2      -               rom (reset)     samram rom (basic)
         3      -               page 0          samram rom (monitor,..)
         4      8000-bfff       page 1          Normal 8000-bfff
         5      c000-ffff       page 2          Normal c000-ffff
         6      -               page 3          Shadow 8000-bfff
         7      -               page 4          Shadow c000-ffff
         8      4000-7fff       page 5          4000-7fff
         9      -               page 6          -
        10      -               page 7          -
        11      Multiface rom   Multiface rom   -
*/
int32_t ZxSpectrum::z80BlockToPage(const uint8_t b, const ZxSpectrumType type) {
  //                                 0   1   2   3   4   5   6   7   8   9  10  11
  const static int8_t z80p48k[]  = {-1, -1, -1, -1,  2,  0, -1, -1,  5, -1, -1, -1};
  const static int8_t z80p128k[] = {-1, -1, -1,  0,  1,  2,  3,  4,  5,  6,  7, -1};

  int32_t p = -1;
  switch(type) {
    case ZxSpectrum48k:
    case ZxSpectrumTurbo:  p = z80p48k[b];  break;
    case ZxSpectrum128k: p = z80p128k[b]; break;
    default: break;
  }  
  return p;
}

/*
    Byte    Length  Description
    ---------------------------
    0       2       Length of compressed data (without this 3-byte header)
                    If length=0xffff, data is 16384 bytes long and not compressed
    2       1       Page number of block
    3       [0]     Data
*/   
int32_t ZxSpectrum::loadZ80MemBlock(InputStream *is, const ZxSpectrumType type) {
 
  int32_t l = is->readWord();
  if (l < 0) return l;
  DBG_SPEC("Found Z80 V2 block of length %ld\n", l);
  int32_t b = is->readByte();
  if (b < 0) {
    DBG_SPEC("Failed to read Z80 V2 block number\n");
    return -2;
  }
  DBG_SPEC("Found Z80 V2 block %ld\n", b);

  if (b >= 11) {
    DBG_SPEC("Invalid Z80 V2 block number %ld\n", b);
    return -2;
  }

  int32_t p = z80BlockToPage(b, type);
  
  if (p == -1) {
      DBG_SPEC("Failed to translate Z80 V2 block %ld\n", b);
      return -2;
  }
  
  uint8_t *m = (uint8_t *)&_RAM[p];
  if (l == 0xffff) {
    int32_t r = is->read(m, 1<<14);
    if (r < 1<<14) {
      DBG_SPEC("Failed to read uncompressed data for Z80 V2 block %ld\n", b);
      return -2;
    }
  }
  else {
    int32_t s = l;
    uint32_t i = 0x0000;
    uint32_t wd = 0;
    uint32_t wf = 0;
    while (i < 0x4000) {
      int32_t r;
      if (s > 0) { r = is->readByte(); s--; } else { r = -1; }
      if (r < -1) return r;
      wd <<= 8; wf <<= 1;
      if (r >= 0) { wd |= r; wf |= 1; }
      if ((wf & 0xf) == 0) break;
      if ((wf & 0xf) == 0xf) {
        if ((wd & 0xffff0000) == 0xeded0000) {
          const uint32_t d = wd & 0xff;
          const uint32_t e = i + ((wd >> 8) & 0xff);
          const uint32_t f = e > 0x4000 ? 0x4000 : e;
          while (i < f) m[i++] = d;
          wf = 0;
        }
      }
      if (wf & 0x8) {
        m[i++] = wd >> 24;
      }
    }
    DBG_SPEC("read %ld compressed block bytes %ld under-run\n", i, s);
  }
  return 0;
}

int32_t ZxSpectrum::loadZ80MemV0(InputStream *is) {
  transmute(type48(ZxSpectrum48k));
  for (uint32_t i = 0x4000; i < 0x10000; ++i) {
    int32_t r = is->readByte();
    if (r < 0) return r;
    writeByte(i, r);
  }
  return -1;
}

int32_t ZxSpectrum::loadZ80MemV1(InputStream *is) {
  transmute(type48(ZxSpectrum48k));
  uint32_t i = 0x4000;
  uint32_t wd = 0;
  uint32_t wf = 0;
  while (i < 0x10000) {
    int32_t r = is->readByte();
    if (r < -1) return r;
    wd <<= 8; wf <<= 1;
    if (r >= 0) { wd |= r; wf |= 1; }
    if ((wf & 0xf) == 0) break;
    if ((wf & 0xf) == 0xf) {
      if (wd == 0x00eded00) break;
      if ((wd & 0xffff0000) == 0xeded0000) {
        const uint32_t d = wd & 0xff;
        const uint32_t e = i + ((wd >> 8) & 0xff);
        const uint32_t f = e > 0x10000 ? 0x10000 : e;
        while (i < f) writeByte(i++, d);
        wf = 0;
      }
    }
    if (wf & 0x8) {
      writeByte(i++, wd >> 24);
    }
  }
  return i - 0x4000;
}

void ZxSpectrum::loadZ80(InputStream *is) {
  // For the Turbo Spectrum clone 48K snapshots always load as a 48K machine so
  // they run correctly without re-entering the boot code.
  ZxSpectrumType t = _type;
  if (t == ZxSpectrumTurbo) t = ZxSpectrum48k;
  reset(t);
  int32_t version = loadZ80Header(is);
  DBG_SPEC("Reading Z80 version %ld\n", version);
  if (version >= 0) {
    switch(version) {
      case 0:
        loadZ80MemV0(is);
        break;
      case 1:
        loadZ80MemV1(is);
        break;
      case 2:
        ZxSpectrumType type;
        if (loadZ80HeaderV2(is, &type) >= 0) {
          while(loadZ80MemBlock(is, type) >= 0);
        }
        break;
      default:
        DBG_SPEC("Reading Z80 version %ld is not implemented!\n", version);
        break;
    }
  }
  is->close();
}

bool ZxSpectrum::saveZ80(OutputStream *os) {
  return writeZ80(os, 3) >= 0;
}

uint32_t ZxSpectrum::tStatesPerMilliSecond() {
  return _moderate > 0 ? (32000 / _moderate) : 4000;
}

void ZxSpectrum::loadTap(InputStream *inputStream) {
  _pulseChain.loadTap(
    inputStream,
    tStatesPerMilliSecond()
  );
}

void ZxSpectrum::loadTzx(InputStream *inputStream) {
  _pulseChain.loadTzx(
    inputStream,
    tStatesPerMilliSecond(),
    _type != ZxSpectrum128k
  ); 
}

void ZxSpectrum::ejectTape() {
  _pulseChain.reset();
}

void ZxSpectrum::pauseTape(bool pause) {
  _pulseChain.pause(pause);
}

void ZxSpectrum::togglePauseTape() {
  _pulseChain.pause(!_pulseChain.paused());
}

bool ZxSpectrum::tapePaused() {
  return !_pulseChain.playing();
}

// See https://en.wikipedia.org/wiki/ZX_Spectrum_graphic_modes
#define TOP_ROWS_UNDISPLAYED 29
// clock cycles per horizontal display line:
// 224 CPU cycles for 48k
// 228 CPU cycles for 128k
#define CPU_CYCLES_PER_LINE 228
// Spectrum scan lines per frame:
// 312 lines for 48k
// 311 lines for 128k
#define SCAN_LINES_PER_FRAME 311

void ZxSpectrum::applyPlus3Map() {
#ifdef ZX_PLUS3_ROM_AVAILABLE
  if (_plus3Latch1ffd & 1) {
    static const uint8_t allRam[4][4] = {
      {0, 1, 2, 3}, {4, 5, 6, 7}, {4, 5, 6, 3}, {4, 7, 6, 3}
    };
    const uint8_t *banks = allRam[(_plus3Latch1ffd >> 1) & 3];
    for (uint8_t page = 0; page < 4; page++)
      setPageaddr(page, (uint8_t*)&_RAM[banks[page]]);
  } else {
    const uint8_t rom = (uint8_t)(((_plus3Latch1ffd & 4) ? 2 : 0) | ((_portMem >> 4) & 1));
    static uint8_t *const roms[4] = {
      (uint8_t*)zx_plus3_rom_0, (uint8_t*)zx_plus3_rom_1,
      (uint8_t*)zx_plus3_rom_2, (uint8_t*)zx_plus3_rom_3
    };
    setPageaddr(0, roms[rom]);
    setPageaddr(1, (uint8_t*)&_RAM[5]);
    setPageaddr(2, (uint8_t*)&_RAM[2]);
    setPageaddr(3, (uint8_t*)&_RAM[_portMem & 7]);
  }
#else
  // Without ROM assets this code path is not exposed by the reset menu.
  setPageaddr(0, (uint8_t*)zx_128k_rom_1);
  setPageaddr(1, (uint8_t*)&_RAM[5]);
  setPageaddr(2, (uint8_t*)&_RAM[2]);
  setPageaddr(3, (uint8_t*)&_RAM[_portMem & 7]);
#endif
}

void __not_in_flash_func(ZxSpectrum::stepScanline)(const uint32_t c) {
  _slc += c;
  while (_slc >= CPU_CYCLES_PER_LINE)
  {
    _slc -= CPU_CYCLES_PER_LINE;
    _sl++;
    if (_sl >= SCAN_LINES_PER_FRAME)
    {
      _sl = 0;
      if (_intSource == SyncToCpu) interrupt();
    }
    if (_sl >= TOP_ROWS_UNDISPLAYED && _sl < (TOP_ROWS_UNDISPLAYED + 240))
    {
      _borderBuf[_sl - TOP_ROWS_UNDISPLAYED] = borderColour();
    }
  }
}

#ifdef EAR_PIN
// audio in and out
uint32_t __not_in_flash_func(ZxSpectrum::step)()
{
  const uint32_t c = z80Step(16);
  uint32_t vA, vB, vC;
  if (_moderate) {
    const uint32_t t32 = MUL32(c, _moderate);
    _ay.step(t32);
    // Audio out
    _ta32 += t32;
    while(_ta32 >= u32pas) {
      if(_audio_ready()) {
        _ay.vol(vA, vB, vC);
        _audio_handler(vA, vB, vC, getBuzzerSmoothed(), getBuzzer(), _mute);
      }
      _ta32 -= u32pas;
    }
    // Audio in
    if (_earInvert ? (_eb == 0) : (~_eb == 0)) {
      if (_earDc++ > 16000) {
          _earInvert ^= 1;
          _earDc = 0;
      }
    }
    else {
      _earDc = 0;
    }    
    _te32 += t32;
    while(_te32 >= u32pes) {
      _ebc++;
      _te32 -= u32pes;
    }
    while(_ebc >= 32) {
      while (!zxSpectrumEarReady());
      _eb = zxSpectrumReadEar();
      _ebc -= 32;
    }
    if (_pulseChain.end()) _ear = (_eb >> _ebc) & 1;
  }
  else {
    if (_audio_ready()) {
      _ay.vol(vA, vB, vC);
      _audio_handler(vA, vB, vC, getBuzzerSmoothed(), getBuzzer(), _mute);
    }
    const uint32_t tu32 = time_us_32() << 5;
    const uint32_t tud = tu32 - _tu32;
    _tu32 = tu32;     
    if (tud) _ay.step(tud);
  }
  stepScanline(c);
  _pulseChain.advance(c, &_ear);
  return c;
}
#else
// audio output
uint32_t __not_in_flash_func(ZxSpectrum::step)()
{
  const uint32_t c = z80Step(32);
  uint32_t vA, vB, vC;
  if (_moderate) {
    const uint32_t t32 = MUL32(c, _moderate);
    _ay.step(t32);
    // Audio out
    _ta32 += t32;
    while(_ta32 >= u32pas) {
      while(!_audio_ready());
      _ay.vol(vA, vB, vC);
      _audio_handler(vA, vB, vC, getBuzzerSmoothed(), getBuzzer(), _mute);
      _ta32 -= u32pas;
    }
  }
  else {
    if (_audio_ready()) {
      _ay.vol(vA, vB, vC);
      _audio_handler(vA, vB, vC, getBuzzerSmoothed(), getBuzzer(), _mute);
    }
    const uint32_t tu32 = time_us_32() << 5;
    const uint32_t tud = tu32 - _tu32;
    _tu32 = tu32;     
    if (tud) _ay.step(tud);
  }
  stepScanline(c);
  _pulseChain.advance(c, &_ear);
  return c;
}
#endif
