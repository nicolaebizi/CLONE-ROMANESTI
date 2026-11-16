// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#pragma once
#pragma GCC optimize ("O2")

#include "Z80.h"
#include "hc2000_rom.h"
#include "ZxSpectrumHcFdc.h"
#include "ZxSpectrumCf.h"
#include "ZxSpectrumKeyboard.h"
#include "ZxSpectrumJoystick.h"
#include "InputStream.h"
#include "OutputStream.h"
#include <pico/stdlib.h>
#include "PulseProcChain.h"
#include "128k_rom_1.h"
#include "128k_rom_2.h"
#include "ZxSpectrumAy.h"
#include "hardware/pwm.h"
#include "ZxSpectrumAudio.h"
#include "ZxSpectrumMouse.h"
#include "CobraFloppy.h"

// #define DEBUG_SPEC
#ifdef DEBUG_SPEC
#define DBG_SPEC(...) printf(__VA_ARGS__)
#else
#define DBG_SPEC(...)
#endif

enum ZxSpectrumType {
  ZxSpectrum48k,
  ZxSpectrum128k,
  ZxSpectrumTurbo,
  ZxSpectrumHc2000,
  ZxSpectrumCobra,
  // Amstrad +3B (menu item only exposed when ZX_PLUS3_ROM_AVAILABLE is set)
  ZxSpectrumPlus3,
  // ZX Spectrum +3s (10th computer, machine #10): Cristian Secară's modified
  // +3 with his own ROM set (NEWEPR0-3) and the custom I/O ports $7F7E/
  // $BF7E/$DF7E/$EF7E. Phase 1: same memory map / FDC / drives / CP/M as
  // the plain +3, plus the port readback latches and the $1FFD speed-report
  // bits. Menu item only exposed when ZX_PLUS3_ROM_AVAILABLE is set.
  ZxSpectrumPlus3s,
  // TIM S Plus (7th computer; boot-only phase, menu entry + transmute/reset
  // arms compile only under PICO_RP2350 so RP2040 builds are unchanged)
  ZxSpectrumTimsPlus,
  // ICE Felix HC-88 (9th computer; phase 1: machine type + memory map +
  // standard 32-col video from the separate VRAM + reset-menu entry).
  // Enabled on RP2040 too (the main RAM is shared bank 0-3 of _RAM; only the
  // 16K VRAM is extra). No FDC and no hires mode this phase.
  ZxSpectrumHc88,
  // ICE Felix CUB-Z (8th computer): CP/M machine, 64K RAM + 16K video RAM
  ZxSpectrumCubz,
  // ZX48 SuperSpec (11th computer; R. Koenig's modified ZX Spectrum, the
  // user's physical build; RP2350 only). Phase 1: 64K EPROM (4 x 16K banks
  // 224-227) paged at $0000-$3FFF by the GAL latches (IN 224..231 / IN 243),
  // MEMLOCK paging lock, RAMSEL-swapped 32K RAM bank at $8000-$FFFF
  // (IN 118/119). Keyboard = standard ULA port 0xFE.
  ZxSpectrumSuperSpec
};

// CoBra 1 mainboard v.0.2 jumper configurations (Jumper settings table).
enum CobraConfig {
  Cobra80K80Col = 0, // "80 chr/lin": 80K DRAM + 80 characters per line module
  Cobra80K      = 1, // "Modif. 80K"
  Cobra64K      = 2  // "Std. (64K)" / "Modif. 64K"
};

enum ZxSpectrumIntSource {
  SyncToDisplay = 0,
  SyncToCpu     = 1
};

// ZX48K custom-ROM loader stage label (rfix4): written by the menu loader
// before each step, printed persistently in the menu header row 1. Declared
// here so the VGA/HDMI main files can read it too.
extern const char* zxRomLoadStage;

class ZxSpectrum {
private:
  Z80 _Z80;
  uint32_t _tu32;
  int32_t _ta32; // audio out timer
  int32_t _te32; // audio in timer
  int32_t u32pas; // Time for a single audio out sample in 32nds of a micro second
  int32_t u32pes; // Time for a single audio in sample in 32nds of a micro second
  uint32_t _moderate;
  ZxSpectrumKeyboard *_keyboard1;
  ZxSpectrumKeyboard *_keyboard2;
  ZxSpectrumJoystick *_joystick;
  ZxSpectrumMouse *_mouse;
  uint8_t _borderColour;
  uint8_t _port254;
  uint8_t _portMem;
  // ZX Spectrum +3: last OUT to port $1FFD (D0 = all-RAM $0000-$3FFF paging,
  // D1/D2 = RAM bank selection, D3 = FDC motor, D4 = disk motor). Applied by
  // applyPlus3Map(); ROM page comes from (_plus3Latch1ffd & 4) | 7FFD D4.
  uint8_t _plus3Latch1ffd;
  // ZX Spectrum +3s: readback latches of the four custom ports. Index 0..3 =
  // $7F7E (ROM/shadow + 48K-lock + H/V border bits), $BF7E (VRAM control +
  // double-H-res), $DF7E (extended RAM paging), $EF7E (clock force/video/
  // Kempston). Writes latch the value, IN returns it; every bit defaults to 0
  // after reset. Phase 2a (NEWEPR.GEN): $7F7E bits0-3 (one-hot, bit n =
  // ROM socket n) map shadow pool banks 8-11 (internal pages 12-15) over
  // $0000-$3FFF when the corresponding ROM slot is the currently paged one
  // (bit4 gates writes there, bit5 = 48K paging lock, latch-only - it does
  // NOT gate the paging port writes, see the L3 hardware-log note in
  // writeByte; bits 6/7 border flags still latch-only) and $DF7E bit3 maps the shadow
  // banks at $C000-$FFFF (pool bank = 8 + ($7FFD & 3)); $EF7E bit7 enables
  // the Kempston joystick port. $BF7E and the remaining bits stay latch-only.
  uint8_t _plus3sLatch[4];
  // Turbo Spectrum: value last OUT to an even port (F5) and the effective
  // mapping config (bits 6:5, CE / SM) applied by applyTurboMap().
  uint8_t _feReg;
  uint8_t _feCfg;
  // ICE Felix HC2000: port 0x7E latch (D0 = 16K ROM half, D1 = map at $E000,
  // D2 = lock) and the C5/C7 CP/M flag.
  uint8_t _hcLatch7e;
  bool _hcCpm;
  bool _hcIf1Shadow;
  ZxSpectrumHcFdc _hcFdc;
  // 8K pages: the CoBra standard 64K map swaps 8K halves.
  uint8_t* _pageaddr[8];

  // CoBra 1 (mainboard v.0.2 rev.3.17e, floppy interface v.1.2)
  CobraFloppy _cobraFloppy;
  CobraConfig _cobraConfig;
  bool _cobraPO;          // U36A: configuration flip-flop, latches A7 (R7) on refresh
  bool _cobraLO6;         // U36B: latches O6 on PO 1->0
  bool _cobra80;          // 80 chr/lin module: O5 latch set in CP/M
  bool _cobraRoBasic;     // JP9 1-2: DRAM#0 read-only in Spectrum mode
  int32_t _cobraPor;      // T-states of NPOR still active after the CPU reset
  uint8_t _cobraPpiCtrl;  // U79 8255 control word
  uint8_t _cobraPortC;    // U79 port C output latch
  uint8_t _cobraWr[8];    // write handling per 8K page

  // ICE Felix CUB-Z (technical manual 1989; reference: Fuse BMC2026 cubz.c)
  CobraFdc _cubzFdc;      // i8272 at $90/$91, data through the 8257 DMA
  CobraDisk _cubzDisk[CobraFdc::DRIVES];
  bool _cubzRom;          // /PROMOFF high: EPROMs overlay $E800-$FFFF
  uint8_t _cubzPioB;      // PIO-B: b2 +VDB (VRAM at $8000), b3 /PROMOFF,
                          //        b6 KBDATA in, b7 keyboard clock out
  uint16_t _cubzDmaAddr;  // 8257 channel 2 address ($84)
  uint16_t _cubzDmaCount; // 8257 channel 2 count ($85, 14 bits, N-1)
  bool _cubzDmaAddrHi;    // 8257 first/last flip-flops
  bool _cubzDmaCountHi;
  bool _cubzDmaActive;    // channel 2 enabled ($88 bit 2)
  bool _cubzDmaTc;        // TC status bit, cleared by reading $88
  uint8_t _cubzScroll;    // VDT-52S SCROLL latch ($F2): first raster line
  uint8_t _cubzCsinch;    // CSINCH LS174 latch ($F3)
  uint8_t _cubzKbdChar;   // VDT-52S serial keyboard: byte being shifted out
  uint8_t _cubzKbdPhase;  // 0 = start bit, 1..8 = data bits
  bool _cubzKbdBit;       // KBDATA line
  uint8_t _cubzKbdQ[16];  // ASCII key queue
  uint8_t _cubzKbdHead;
  uint8_t _cubzKbdTail;
  // $E000-$FFFF read page while the EPROMs are enabled: $E000-$E7FF RAM
  // (U6.4 not fitted), $E800-$FFFF EPROM.  Writes always reach the RAM.
  uint8_t _cubzTop[0x2000];
#if PICO_RP2350
  // TIM-S Plus (boot-only, RP2350 only): motherboard register panel latches.
  // Reset values follow fuse machines/tims_reset(): everything 0 except the
  // 8255 control word 0x9b (all ports gated off until the ROM does a mode set).
  uint8_t _timsPaging;    // OUT $7FFD custom latch (B5 is NOT a paging lock)
  uint8_t _timsSecondary; // OUT $1FFD (bit3 motor: latch only in boot phase)
  uint8_t _timsPc;        // 8255 port C
  uint8_t _timsPa;        // 8255 port A
  uint8_t _timsPb;        // 8255 port B
  uint8_t _timsControl;   // 8255 control word
  uint8_t _timsScroll;    // OUT $5CFD scroll latch (no raster semantics yet)
  uint8_t _timsPages[4];  // 16K system pages currently mapped per slot
  // Diag counters (strippable later): writeByte dual-store VRAM mirror count.
  uint32_t _wrVram;       // dual-store mirror activations in writeByte
  // diag6: post-load write-op trace ring. Captures the low 8 bits of the
  // port address plus the written value for every TIM-mode port write, so
  // the disk menu can show what the boot ROM did after a clean load.
  struct TimsWriteOp { volatile uint8_t port_hi; volatile uint8_t val; };
  volatile TimsWriteOp _opRing[16];
  uint8_t _opIdx = 0;     // newest-first overwrite index counter (wraps)
  uint32_t _brWr = 0;     // 0xFE border writes while in TIM mode
  // diag7 phase 2: data-port READ ring. Captures the VALUE of every read of
  // the TIM FDC data port ($3FFD, readIO readData path) newest-first, so the
  // disk menu can show the result-phase bytes the boot ROM saw.
  volatile uint8_t _fdRd[8];
  uint8_t _rdIdx = 0;     // newest-first overwrite index counter (wraps)
  // fix3 phase 2: TIM-mode IO read counters (volatile uint32_t, TIM-gated:
  // every increment site sits inside a ZxSpectrumTimsPlus read path).
  volatile uint32_t _rd2FFD = 0; // $2FFD MSR (FDC main status) reads
  volatile uint32_t _rdCFD  = 0; // $0CFD (8255 port C) reads
  volatile uint32_t _rdPB   = 0; // $0EFD + $0FFD reads combined
  volatile uint32_t _rdFE = 0; // $FE (ULA keyboard) reads
#endif
#ifdef ZX_PLUS3_ROM_AVAILABLE
  // diag10: +3s port WRITE/READ ring. Logs the last 256 port events on the
  // +3s machine only: writes to 1FFD/7FFD/7F7E/BF7E/DF7E/EF7E and reads of
  // the same ports (incl. the 1FFD/7FFD readbacks - the read values are
  // what the stock ROM's IN-modify-OUT paging loops feed back into the
  // next OUT). A few cycles per event, pushed from the readIO/writeIO
  // Plus3s branches and shown on the menu diag lines at the hang point.
  // diag12: ring enlarged 24 -> 256 entries (4B each = 1KB RAM) so the
  // menu can show a window 40 events back (idx-45..idx-41) covering the
  // loader's mid-sequence that the old 24-entry ring overwrote with the
  // later boot writes. The same two push sites (readIO/writeIO Plus3s
  // branches) still feed it, one entry per event.
  struct Plus3sPortOp { volatile uint16_t port; volatile uint8_t val; volatile uint8_t isRead; };
  volatile Plus3sPortOp _s3Ops[256];
  uint8_t _s3Idx = 0;     // overwrite index counter (wraps)
#endif
  // ICE Felix HC88 (phase 1; reference fuse machines/hc88.c):
  // the 64K main RAM is the low half of the shared base _RAM pool (banks
  // 0-3 of eight being always allocated and memset on every machine reset;
  // only one machine type is live at a time) plus a strictly separate 16K
  // VRAM and the 16K motherboard ROM (hc88_rom.h). Keeping main RAM inside
  // the base pool means the only new RAM for HC88 is the 16K VRAM, which
  // also fits the RP2040 builds.
  // _hc88Latch mirrors the write-only motherboard latch, partially decoded
  // by A7,A6,A0 = 1,0,1 ((port & 0xC1) == 0x81, fuse hc88.c:109 and
  // machines_periph.c:262). Bit semantics as implemented (fuse hc88.c):
  //   b0: when clear the ROM overlays $0000-$3FFF for READS only, writes
  //       still reach main RAM bank 0 beneath it (hc88.c:89-90); when set,
  //       Q0 is RAM.
  //   b1: LOCK, inhibits writes in the whole of Q0 (hc88.c:86-88).
  //   b3: MOD1, replaces every quadrant with the VRAM (hc88.c:63-66).
  //   b4: hires, stored only - the display stays 32-col this phase.
  //   b5: MOD0 - when clear the VRAM is mapped in Q1 only (hc88.c:67-69).
  // _hc88If1Paged is the U37A IF1 shadow-RAM latch: armed by an M1 fetch in
  // $0008-$000F / $1708-$170F, released by an M1 fetch in $0700-$0707
  // (fuse hc88.c:114-124, implemented in readOpcode via the fetch hook).
  uint8_t _hc88Vram[1 << 14];
  uint8_t _hc88Latch;
  bool _hc88If1Paged;
  // HC88 diag counters (volatile, small, strippable; TIM-diag style,
  // cumulative across resets like the TIM counters):
  volatile uint32_t _hc88LatchWr = 0;  // writes to the motherboard latch port ((port & 0xC1) == 0x81)
  volatile uint32_t _hc88VramWr = 0;   // byte writes landing in _hc88Vram via the HC88 write path
  volatile uint32_t _hc88Q0Wr = 0;     // writes landing in Q0 RAM beneath the ROM overlay
  volatile uint8_t _hc88LatchLast = 0; // last value written to the motherboard latch
#if PICO_RP2350
  // ZX48 SuperSpec (R. Koenig's machine; GAL SUPERSPEC.PLD + the decoded
  // port table in roeloef/STUDIU_EMULARE_ROELOF.md):
  // _ssEpBank: EPROM bank window at $0000-$3FFF. The GAL latches epA14/15/16
  //   from A0/A1/A2 of any IN with A7=1, A3=0 (transparent during the read,
  //   held otherwise, frozen while MEMLOCK). The physical board has a 64K
  //   EPROM (27C512) with epA16 unwired, so the effective bank = latch & 3
  //   (banks 224-227 of supr_rom; IN 228..231 alias to 224..227). The latch
  //   stores the full 3 bits (a & 7) for state fidelity.
  // _ssRamsel: RAMSEL latch (IN 118 = 0, IN 119 = 1; any IN with A7=0, A3=0
  //   drives it from A0). Selects which 32K RAM shows at $8000-$FFFF.
  // _ssMemlock: MEMLOCK latch (set by any IN with A7=1, A4=1, A3=0, e.g.
  //   IN 243 = bank 227 + lock; "227+16" in the ROM source). Set-only: it
  //   has no GAL reset term, only /RESET releases it.
  // _ssWeArmed: the epromwe write-protect flip-flop (D=reset, clocked by
  //   any RAMSEL pulse). Latch-only this phase: there is no writable flash
  //   emulation, the EPROM window is always read-only.
  uint8_t _ssEpBank;
  bool _ssRamsel;
  bool _ssMemlock;
  bool _ssWeArmed;
  // The extra 32K RAM bank (4164 pair added by Koenig in place of the 48K
  // machine's upper RAM chips): shown at $8000-$FFFF while RAMSEL = 1. DOS
  // system variables live here (Ooffset1..4 at $C022-$C025).
  // Storage: RELOCATED OUT OF BSS into the TIM-S pool's banks 8-9 (BSS fix:
  // the +32K array collapsed the RP2350 heap margin and killed the scanvideo
  // runtime buffer allocations -> no VGA). The pool has no truly free banks
  // (12/12 used: 0-7 TIM system RAM, 8-11 TIM system RAM AND the +3s shadow
  // pages), so SuperSpec shares banks 8-9 with the +3s shadow slots 0-1 -
  // the pools are machine-exclusive (only one machine type runs at a time)
  // and each machine zeroes its own range on reset (SuperSpec: banks 8-9;
  // +3s: banks 8-11; TIM: the whole pool), so no state bleeds across a
  // machine switch (same rationale as the +3s/TIM pool sharing).
  inline uint8_t* ssExtra() { return (uint8_t*)&_timsRAM[8][0]; }
#endif
  // ZX48 SuperSpec phase 2: the machine's own CompactFlash in MEMORY MODE,
  // backed by a raw image file on the emulator's SD card (ZxSpectrumCf).
  // Attached by the platform main files via setCf(); a null pointer (or a
  // missing image file) leaves the CF ports returning the no-media state.
  ZxSpectrumCf* _ssCf = 0;
public:
  void setCf(ZxSpectrumCf* cf) { _ssCf = cf; }
  // Live CF state for the menu diag line (fix5, phase 2): false when the
  // SuperSpec CF is not attached on this build.
  bool cfDiag(ZxSpectrumCf::CfDiag* d) const {
    if (!_ssCf) return false;
    _ssCf->diag(d);
    return true;
  }
private:
  bool _ear;
  uint32_t _earInvert;
  uint32_t _earDc;
  uint32_t _eb; // 32 audio in samples
  uint32_t _ebc; // index into the above
  
  PulseProcChain _pulseChain;
  
  ZxSpectrumAy _ay;
  ZxSpectrumType _type;
  ZxSpectrumIntSource _intSource;
  uint32_t _modMul;
  bool _mute;
  int32_t _buzzer;
  int32_t _buzzerl;
  int32_t _bzf;
  uint32_t _sl;
  uint32_t _slc;
  uint32_t _fc;  // Screen flip count
  uint32_t _fcf; // Screen flip count per frame
  zx_spectrum_audio_handler_t _audio_handler;
  zx_spectrum_audio_ready_t _audio_ready;
  zx_spectrum_audio_driver_t* _audio_driver;

  volatile uint8_t _borderBuf[240];       //Border Buffer 240 lines

  uint32_t tStatesPerMilliSecond();
  
  inline void cobraClock(uint32_t k) {
    if (_cobraPor > 0 && (_cobraPor -= (int32_t)k) <= 0) {
      // NPOR released: the 8255 leaves reset (all ports inputs).
      _cobraPor = 0;
      cobraPortChanged();
    }
    _cobraFloppy.clock(k);
    const bool i = _cobraFloppy.intLine();
    if (i != (bool)_Z80.int_line) z80_int(&_Z80, i);
  }

  inline uint32_t z80Step(uint32_t tstates) {  
    uint32_t t = 0;
    while(true) {
      const uint32_t k = z80_run(&_Z80, 6);
      if (_type == ZxSpectrumCobra) cobraClock(k);
      else if (_type == ZxSpectrumCubz) _cubzFdc.clock(k);
      _bzf += k;
      t += k;
      const int32_t j = (int32_t)((_port254 >> 3) & 2) - 1;
       while(_bzf > 4) {
        _buzzer -= _buzzer >> 6;
        _buzzerl -= _buzzerl >> 10;
        _buzzer += MUL32(j, 95);
        _buzzerl += MUL32(j, 1024);
        _bzf -= 4;
      }
      if (t >= tstates ) return t;
    }
  }
  
  inline void z80Power(bool state) {
    z80_power(&_Z80, true);
  }
  
  inline void z80Reset() {
    z80_instant_reset(&_Z80);
  }
  
  inline void setPageaddr(uint32_t page, uint8_t *ptr) {
    uint8_t *p = (uint8_t*)((uintptr_t)ptr - (page << 14));
    _pageaddr[page << 1] = p;
    _pageaddr[(page << 1) + 1] = p;
  }

  inline void setPage8(uint32_t page, const uint8_t *ptr) {
    _pageaddr[page] = (uint8_t*)((uintptr_t)ptr - (page << 13));
  }
  
  inline uint8_t* memaddr(uint32_t address) {
    // Custom 48K ROM (R menu option, 16K image loaded from zxspectrum/roms):
    // while a user ROM is loaded, the 48K machine reads/fetches the whole
    // $0000-$3FFF region from the RAM buffer instead of the built-in flash
    // ROM page. The hot path pays exactly one pointer check; the 48K machine
    // never re-pages Q0 (port $7FFD is 128K/+3 only) and never writes the
    // low 16K, so the buffer cannot be corrupted from the emulated bus.
    // Other machine types (128K/Turbo/HC2000/...) are untouched.
    if (_customRom && _type == ZxSpectrum48k && address < 0x4000) {
      return _customRom + address;
    }
    // HC2000: while the Interface I v2 ROM occupies the shadow slot the CPU
    // reads the IF1 page over $0000-$3FFF (the full 16K lives in _RAM[7]),
    // except the key/board scratch ranges which come from _RAM[6].
    if (_type == ZxSpectrumHc2000 && _hcIf1Shadow && !(_hcLatch7e & 2) && address < 0x4000) {
      if ((address >= 0x2800 && address < 0x3000) || (address >= 0x3800 && address < 0x4000)) {
        const uint32_t shadowOffset = address >= 0x3800 ? address - 0x3800 : address - 0x2800;
        return (uint8_t*)&_RAM[6] + (shadowOffset & 0x03ff);
      }
      return (uint8_t*)&_RAM[7] + address;
    }
    if (_type == ZxSpectrumHc2000 && address >= 0xC000) {
      if (address >= 0xE000) {
        if (_hcLatch7e & 2)
          return (uint8_t*)hc2000_main_rom + (((_hcLatch7e & 1) << 14) + 0x2000 + (address - 0xE000));
        // CP/M: page 7 mirrors the same physical 8 KiB as page 6
        // (MemoryPaging.cs: pages[6]=ram+0xE000, pages[7]=ram+0xE000).
        return (uint8_t*)&_RAM[0] + 0x2000 + (address - 0xE000);
      }
      else if (_hcCpm) return (uint8_t*)&_RAM[0] + 0x2000 + (address - 0xC000);
    }
    return address + _pageaddr[address >> 13];
  }
  
  inline uint8_t readByte(uint16_t address) {
    return *memaddr(address);
  }
  
  enum CobraWrite { CobraWrRom = 0, CobraWrRam = 1, CobraWrVram = 2 };

  // CUB-Z: every address is RAM for writes, also beneath the EPROMs.
  inline void cubzPoke(uint16_t address, uint8_t value) {
    _RAM[address >> 14][address & 0x3FFF] = value;
    if (address >= 0xE000 && address < 0xE800) _cubzTop[address - 0xE000] = value;
  }

  inline void writeByte(uint16_t address, uint8_t value) {
    if (_type == ZxSpectrumCubz) {
      if (address >= 0x8000 && address < 0xC000 && (_cubzPioB & 0x04)) *(memaddr(address)) = value;
      else cubzPoke(address, value);
      return;
    }
    if (_type == ZxSpectrumCobra) {
      const uint8_t w = _cobraWr[address >> 13];
      if (w == CobraWrRom) return;
      *(memaddr(address)) = value;
      // Spectrum mode, 80K: a write to 4000-5FFF also goes into the video RAM.
      if (w == CobraWrVram) _RAM[5][address & 0x1FFF] = value;
      return;
    }
#if PICO_RP2350
    // ZX48 SuperSpec writes: $0000-$3FFF is the EPROM bank window with no
    // RAM beneath it - writes are dropped (the GAL epromwe flip-flop holds
    // the EPROM write-protected after the first RAMSEL pulse; latch-only
    // this phase, the window is always read-only). $4000-$7FFF is the fixed
    // RAM and $8000-$FFFF the RAMSEL-selected 32K bank, both plain RAM
    // through the page table applied by applySuperSpecMap().
    if (_type == ZxSpectrumSuperSpec) {
      if (address < 0x4000) return;
      *(memaddr(address)) = value;
      return;
    }
#endif
#if PICO_RP2350
    // TIM-S Plus writes. Translation of fuse memory_pages.c writebyte_internal
    // (VM order: the tims_plus_video_write VRAM hook runs first, then the
    // write-map store) together with tims_plus_memory_map writable gating:
    // - $0000-$3FFF: the low ROM only replaces the READ map (memory_map_
    //   _16k_read_write(...,1,0) keeps the old write map alive), so writes go
    //   through to the system bank mapped under it (_timsPages[0], 0..11).
    //   That bank is write-gated 8..11 unless 8255 PC bit1 is set
    //   (fuse tims_plus.c lines 59-60).
    // - CP/M Vol-III ($0000-$5FFF clear, PC bit4) $4000-$5FFF: read map is the
    //   separate VRAM 8K half (set in applyTimsMap), but the write map stays
    //   on system page 5 AND the VRAM hook stores the byte too; PA0 inhibits
    //   attribute writes into the VRAM only (tims_plus.c:75).
    if (_type == ZxSpectrumTimsPlus) {
      if (address < 0x4000) {
        const uint8_t p0 = _timsPages[0];
        if (p0 >= 8 && p0 < 12 && !(_timsPc & 2)) return;
        _timsRAM[p0][address] = value;
        return;
      }
      if ((_timsPc & 16) && !(_timsSecondary & 1) && address < 0x6000) {
        ++_wrVram; // diag: count the dual-store VRAM mirror activation
        const uint16_t inHalf = address & 0x1FFF;
        if (!((_timsPa & 1) && inHalf >= 0x1800 && inHalf < 0x1B00))
          _timsVram[inHalf + ((_timsPaging & 8) ? 0x2000 : 0)] = value;
        _timsRAM[5][address - 0x4000] = value;
        return;
      }
      *(memaddr(address)) = value;
      return;
    }
#endif
    // HC88 writes (fuse machines/hc88.c:56-92). Sub-16K quadrant map, so
    // only Q0 ($0000-$3FFF) needs special handling; Q1-Q3 are plain RAM /
    // VRAM reads+writes through the CPU page table (applyHc88Map):
    //  - MOD1 (b3): the VRAM occupies the whole quadrant, writes included.
    //  - LOCK (b1) inhibits all Q0 writes unless the IF1 shadow is armed or
    //    MOD1 is set (fuse hc88.c:86-88).
    //  - IF1 shadow armed (b0 set): Q0 writes land in main RAM bank 1
    //    (MUXA14, fuse hc88.c:71-72).
    //  - ROM overlay (b0 clear): the read map shows the ROM but writes keep
    //    reaching main RAM bank 0 beneath it (fuse memory_map_16k_read_
    //    write(...,1,0) at hc88.c:89-90).
    if (_type == ZxSpectrumHc88) {
      if (address < 0x4000) {
        if ((_hc88Latch & 0x02) && !_hc88If1Paged && !(_hc88Latch & 0x08)) return; // LOCK
        if (_hc88Latch & 0x08) { ++_hc88VramWr; _hc88Vram[address] = value; return; } // MOD1: Q0 = VRAM (diag)
        if ((_hc88Latch & 0x01) && _hc88If1Paged) { _RAM[1][address] = value; return; } // shadow = bank 1
        ++_hc88Q0Wr; // diag: write lands in Q0 RAM beneath the ROM overlay
        _RAM[0][address] = value;                                                  // beneath the ROM
        return;
      }
      *(memaddr(address)) = value;
      return;
    }
#if PICO_RP2350
    // ZX Spectrum +3s (phase 2a, NEWEPR.GEN): a shadow pool bank is mapped
    // at $0000-$3FFF (7F7E one-hot bit n = ROM socket n, normal paging mode
    // only, and only when that socket is the currently selected ROM - same
    // condition as applyPlus3sMap, so writes land in the same bank the CPU
    // reads/fetches). Writes into the shadowed window are gated by bit4
    // ("write-enable in ROM mode": 0 = enabled, 1 = disabled - the NEWEPR.GEN
    // EXIT routine sets it BEFORE the copies, and the copies go through
    // $C000, which stays writable). With no shadow page mapped for the
    // current socket, the standard ROM write-protection below applies. The
    // shadow pages physically live in the RP2350-only TIM-S pool's banks
    // 8-11 (machine-exclusive pools; see applyPlus3sMap; banks 8-9 also
    // host SuperSpec's extra 32K RAM - ssExtra(), per-machine reset zeroing
    // keeps the states apart).
    if (_type == ZxSpectrumPlus3s && address < 0x4000 && !(_plus3Latch1ffd & 0x01)) {
      const uint8_t wrSlot = (uint8_t)(((_plus3Latch1ffd & 4) ? 2 : 0) | ((_portMem >> 4) & 1));
      if (_plus3sLatch[0] & (1 << wrSlot)) {
        if (!(_plus3sLatch[0] & 0x10)) {
          _timsRAM[8 + wrSlot][address] = value;
        }
        return;
      }
    }
#endif
    if (address < 0x4000) {
      // Turbo Spectrum: $0000-$3FFF is ROM only while FE_CFG=00. After the
      // boot switches to 64K (20) or Spectrum (60) mode it is RAM and must
      // accept writes.
      const bool hcLowRam = _type == ZxSpectrumHc2000 && (_hcLatch7e & 2);
      const bool hcIf1Ram = _type == ZxSpectrumHc2000 && _hcIf1Shadow && !(_hcLatch7e & 2) &&
        ((address >= 0x2800 && address < 0x3000) || (address >= 0x3800 && address < 0x4000));
      // ZX Spectrum +3 special paging mode (port $1FFD bit 0): applyPlus3Map()
      // pages RAM banks over $0000-$3FFF, so the low 16 KiB is RAM and must
      // accept writes (all-RAM mode, page 0 maps to _RAM[0]).
      const bool plus3AllRam = (_type == ZxSpectrumPlus3 || _type == ZxSpectrumPlus3s) &&
        (_plus3Latch1ffd & 0x01);
      if (!hcLowRam && !hcIf1Ram && !plus3AllRam && !(_type == ZxSpectrumTurbo && (_feCfg & 0x60) != 0x00)) return;
    }
    // HC2000: with latch D1 the roman overlay is ROM; writes are lost
    // (reference: pages[7] Writable=false, HcMemory.Write drops them).
    if (_type == ZxSpectrumHc2000 && (address & 0xE000) == 0xE000 && (_hcLatch7e & 2)) return;
    *(memaddr(address)) = value;
  }

  inline uint8_t readIO(uint16_t address)
  {
    if (_type == ZxSpectrumCobra) return cobraReadIO(address);
    if (_type == ZxSpectrumCubz) return cubzReadIO(address);
#if PICO_RP2350
    // ZX48 SuperSpec: the GAL latches are driven by every IN (the set terms
    // all require !iorq & !rd; OUT cycles only hold, rd=1 in the hold terms).
    // The GAL only has A0-A7 as inputs, so the decode is on the LOW byte -
    // for IN A,(n) both bus halves carry n, for IN r,(C) it is C. Semantics
    // (SUPERSPEC.PLD + STUDIU_EMULARE_ROELOF.md §4):
    //  - A7=1, A3=0: EPROM bank latch epA16/15/14 <- A2/A1/A0 (IN 224..231,
    //    also 0x90/0xF0 families and IN 243), masked to & 3 (27C512, epA16
    //    unwired). Frozen while MEMLOCK.
    //  - A7=1, A4=1, A3=0: MEMLOCK set (set-only latch, e.g. IN 243 = bank
    //    227 + lock, IN 0x90 family). Released only by reset.
    //  - A7=0, A3=0: RAMSEL <- A0 (IN 118 = bank 0, IN 119 = extra bank;
    //    every such IN drives it, Kempston 0x1F included - authentic GAL
    //    behaviour, no joystick awareness on the board). Frozen while
    //    MEMLOCK; each RAMSEL transition clocks the epromwe flip-flop into
    //    the write-protected state (latch-only).
    // Even ports (0x76/0x77, ULA 0xFE) fall through to the standard ULA
    // keyboard path below; odd unmatched ports return open bus (0xFF) - the
    // bank-switch result in A is not used by the ROM.
    if (_type == ZxSpectrumSuperSpec) {
      const uint8_t a = (uint8_t)address;
      if (!_ssMemlock) {
        if ((a & 0x88) == 0x80) {
          const uint8_t bank = a & 0x07;
          if (bank != _ssEpBank) { _ssEpBank = bank; applySuperSpecMap(); }
        }
        if ((a & 0x88) == 0x00) {
          const bool r = (a & 0x01) != 0;
          if (r != _ssRamsel) { _ssRamsel = r; _ssWeArmed = true; applySuperSpecMap(); }
        }
        if ((a & 0x98) == 0x90) {
          _ssMemlock = true;
        }
      }
      // Phase 2: CompactFlash in MEMORY MODE. GAL cfread active term:
      // !epA15 & !epA14 & !iorq & !rd & !A7 & A3 - the CF is only decoded
      // while the EPROM bank latch holds the 224 family (bank bits 0-1
      // clear, epA14=bit0/epA15=bit1 of _ssEpBank); low byte A7=0, A3=1
      // (0x??7F); the register index comes from A8/A14/A15. Any other
      // combination is an undecoded port (floating bus 0xFF through the
      // default below).
      if ((a & 0x88) == 0x08 && _ssCf && (_ssEpBank & 3) == 0) {
        return _ssCf->readReg(zxSuperSpecCfRegIndex(address));
      }
      // no return: ULA (even ports) and the open-bus default below apply
    }
#endif
#if PICO_RP2350
    // TIM-S Plus motherboard ports (fuse machines_periph.c tims_io_ports,
    // exact 16-bit decode): Even ports stay on the standard ZX ULA keyboard
    // path; odd ports are the register panel. $2FFD = FDC main status register
    // (MSR) and $3FFD = FDC data port (fuse tims_fdc_status/tims_fdc_read);
    // $FFFD/$BFFD have no AY on the TIM-S motherboard and read as open bus
    // ($FF) like any unmapped port.
    if (_type == ZxSpectrumTimsPlus && (address & 0x0001)) {
      // fix3 phase 2: TIM-gated read counters (menu row "13. IO 2F:.. CF:..
      // PB:.. KE:.."). Each port counted exactly once per read.
      if (address == 0x2FFD) ++_rd2FFD;
      else if (address == 0x0CFD) ++_rdCFD;
      else if (address == 0x0EFD || address == 0x0FFD) ++_rdPB;
      if (address == 0x0CFD) return _timsPc;
      if (address == 0x0EFD) return _timsPa;
      if (address == 0x0FFD) return (_timsControl & 2) ? 0xFF : _timsPb;
      if (address == 0x2FFD) {
        const uint8_t msr = _hcFdc.readStatus();
#ifdef COBRA_FDC_TRACE
        static uint8_t last = 0x5A;
        if (msr != last) { printf("TIMS MSR %02X PC=%04X\n", msr, Z80_PC(_Z80)); last = msr; }
#endif
        return msr;
      }
      if (address == 0x3FFD) {
        // diag7 phase 2: capture the value of every data-port read
        const uint8_t v = _hcFdc.readData();
#ifdef COBRA_FDC_TRACE
        printf("TIMS FDC -> %02X PC=%04X\n", v, Z80_PC(_Z80));
#endif
        _fdRd[_rdIdx & 7] = v;
        ++_rdIdx;
        return v;
      }
      return 0xFF;
    }
#endif
    // HC88 reads (fuse hc88.c:94-101 + machines_periph.c:257-260): the FDC
    // ports are decoded EXACTLY on the low byte (periph mask 0x00FF) - $DD
    // is the i8272 main status register, $DF the data (read) port and $E7
    // returns the motor line state (hc88_fdc.c:61 hc88_fdc_motor_state,
    // returned as 0x00/0x01). Any other odd port is open bus and the
    // AY/Kempston decodes further down must not fire for this machine.
    // Even ports take the standard (uncontended) ULA keyboard path below
    // (spec48_port_from_ula, no contention, fuse hc88.c:26-28).
    if (_type == ZxSpectrumHc88 && (address & 0x0001)) {
      switch (address & 0x00FF) { // exact low-byte decode, fuse mask 0x00ff
        case 0x00DD: return _hcFdc.readStatus();
        case 0x00DF: return _hcFdc.readData();
        case 0x00E7: return _hcFdc.dbgMotor() ? 1 : 0; // latch b3 echo
        default: return 0xFF;
      }
    }
#ifdef ZX_PLUS3_ROM_AVAILABLE
    // ZX Spectrum +3s (phase 2a fix): the four motherboard ports decode on
    // the exact 16-bit address and are EVEN (bit0 clear), so they must be
    // matched BEFORE the standard even-port ULA keyboard path below. The
    // NEWEPR.GEN EXIT routine reads the latches back (IN A,(#7F7E), IN
    // D,(#DF7E), IN A,(#EF7E)); when these INs fell through to the ULA path
    // they returned keyboard data (idle row mask $7F -> $BF, $FF with EAR),
    // which the routine ORs #10 into and latches: 7F7E=$BF set bits0-3
    // (shadow page at $0000-$3FFF in place of the ROM) AND bit5 (the 48K
    // paging lock), and DF7E=$BF left bit3 stuck - the loader returned into
    // the zeroed shadow page, NOP-sledged into the screen at $4000 and the
    // machine destroyed itself ("16K of garbage over the screen").
    if (_type == ZxSpectrumPlus3s) {
      switch (address) {
        // diag10: log the latch readback INs too (NEWEPR.GEN EXIT reads
        // these; the returned bytes drive its OR/OUT cycles).
        case 0x7F7E: plus3sOpPush(address, _plus3sLatch[0], 1); return _plus3sLatch[0];
        case 0xBF7E: plus3sOpPush(address, _plus3sLatch[1], 1); return _plus3sLatch[1];
        case 0xDF7E: plus3sOpPush(address, _plus3sLatch[2], 1); return _plus3sLatch[2];
        case 0xEF7E: plus3sOpPush(address, _plus3sLatch[3], 1); return _plus3sLatch[3];
      }
    }
#endif
    if (!(address & 0x0001)) {
#if PICO_RP2350
      // fix3 phase 2: count $FE (ULA keyboard) reads in TIM mode only.
      if (_type == ZxSpectrumTimsPlus && address == 0x00FE) ++_rdFE;
#endif
      // Turbo Spectrum: any IN from an even port loads FE bits 6:5 (Q6/Q5)
      // into the SM/CE flops; remap only when the effective config changes.
      // Once an IN sets SM=1 both flops latch to 1 until RST, so Spectrum
      // mode can never be left by later OUT/IN (FE).
      if (_type == ZxSpectrumTurbo && !(_feCfg & 0x40)) {
        const uint8_t cfg = _feReg & 0x60;
        const uint8_t eff = (cfg & 0x40) ? 0x60 : cfg; // SM=1 latches CE+SM to 1
        if (eff != _feCfg) {
          _feCfg = eff;
          applyTurboMap();
        }
      }
      uint8_t kb = _keyboard1->read(address);
      if (_keyboard2) kb &= _keyboard2->read(address);
      // HC2000 ULA: matches the reference (Ula.Read returns 0xa0 | keyboard | tape<<6).
      if (_type == ZxSpectrumHc2000)
        return 0xa0 | (kb & 0x1f) | (((_ear ^ _earInvert) & 1) << 6);
      return (kb & 0xBF) | (((_ear ^ _earInvert) << 6) & (1 << 6));
    }else
    if (_type == ZxSpectrumHc2000 && (address & 0x00FF) == 0x0085) {
      // HC2000 FDC status register
      return _hcFdc.readStatus();
    }else
    if (_type == ZxSpectrumHc2000 && (address & 0x00FF) == 0x0087) {
      // HC2000 FDC data register
      return _hcFdc.readData();
    }else
    if (_type == ZxSpectrumHc2000 && (address & 0x00FD) == 0x0005) {
      // HC2000 drive latch readback
      return _hcFdc.readLatch();
    }else
    if (_type == ZxSpectrumPlus3 && (address & 0xF002) == 0x2000) {
      return _hcFdc.readStatus();
    }else
    if (_type == ZxSpectrumPlus3 && (address & 0xF002) == 0x3000) {
      return _hcFdc.readData();
    }else
#ifdef ZX_PLUS3_ROM_AVAILABLE
    // ZX Spectrum +3s custom I/O (NEWEPR.GEN / cs alternatively-website):
    // the four motherboard ports decode on the exact 16-bit address and
    // IN returns the latched value (all bits 0 after reset). Phase 1 is
    // readback only - the latched bits do not change anything yet. The
    // +3s ROM2 (+3DOS) delay routines (0x2D00-0x2D99) also read the CPU
    // clock from bits 5-7 of IN $1FFD (bit5 = 3.5MHz, bit6 = 7MHz, bit7 =
    // 14MHz): we emulate the 3.5MHz-equivalent speed, so bit5 reads 1 and
    // bits 6/7 read 0. The plain +3 keeps its old behaviour (open bus).
    if (_type == ZxSpectrumPlus3s) {
      // The four motherboard ports ($7F7E/$BF7E/$DF7E/$EF7E, all EVEN) are
      // matched earlier in this function, before the even-port ULA path;
      // this odd-port branch only handles the +3 FDC ports and the $1FFD
      // readback (NEWEPR.GEN also INs from $1FFD for the speed bits).
      if ((address & 0xF002) == 0x2000) return _hcFdc.readStatus();
      if ((address & 0xF002) == 0x3000) return _hcFdc.readData();
      if ((address & 0xF002) == 0x1000) {
        // diag10: 1FFD readback IN (speed bits) goes into the port ring.
        const uint8_t v = (_plus3Latch1ffd & 0x1F) | 0x20;
        plus3sOpPush(address, v, 1);
        return v;
      }
    }else
#endif
    if (address == 0xFFFD) {
      return _ay.readData();
    }else
    if (address == 0x7FFD && (_type == ZxSpectrum128k || _type == ZxSpectrumPlus3 ||
        _type == ZxSpectrumPlus3s)) {
      // +3s (phase 2a, required by NEWEPR.GEN): IN #7FFD returns the paging
      // latch readback with NO side effect. The 128K/+3 "reading latches #FF"
      // quirk would set the paging lock and then ignore the loader's OUT (the
      // GEN EXIT routine does IN E,(C) from #7FFD and restores it, and the
      // +3s ROM additions use IN A,(C) / OR #10 / OUT (C),A cycles).
      if (_type == ZxSpectrumPlus3s) {
        // diag10: 7FFD readback IN goes into the port ring (the value the
        // UTL-style IN E,(#7FFD); OUT (#E&F8)|n loops see).
        plus3sOpPush(address, _portMem, 1);
        return _portMem;
      }
      // reading #7FFD port is the same as writing #FF into it.
      uint8_t value = 0xFF;
      if ((_portMem & 0x20) == 0) {
        _portMem = value;
        if (_type == ZxSpectrumPlus3) applyPlus3Map();
        else if (_type == ZxSpectrumPlus3s) applyPlus3sMap();
        else {
          setPageaddr(3, (uint8_t*)&_RAM[value & 7]);
          setPageaddr(0, (uint8_t*)((value & 0x10) ? zx_128k_rom_2 : zx_128k_rom_1));
        }
      }
      return 0xFF;
    }

    // ZX Spectrum +3s (phase 2a): the Kempston joystick interface is enabled
    // by $EF7E bit7 (cs port tables, wired to the menu-selected joystick
    // source). While the bit is clear the joystick port is open bus. The
    // clock bits ($EF7E bits0-1) stay report-only (latched) this phase.
    if (_type == ZxSpectrumPlus3s && (address & 0x00E0) == 0 &&
        !(_plus3sLatch[3] & 0x80)) {
      return 0xFF;
    }

    if ((!(address==0x7FFD))&&(!(address&0x00E0))) {
       return (_joystick ? _joystick->getKempston() : 0) | (_mouse ? _mouse->getKempston() : 0);
    }

    // Kempston mouse
    // 64479	FBDF	xxxx x011 xx0x xxxx	X axis	00-FF Roll over at 0 and $FF
    // 65503	FFDF	xxxx x111 xx0x xxxx	Y Axis	00-FF Roll over at 0 and $FF
    // 64223	FADF	xxxx x010 xx0x xxxx	Buttons	D0:Right D1:Left D2:Middle (Some compatables only)
    if ((address | 0b1111100011011111) == 0xFBDF) {
      return _mouse->xAxis();
    }
    if ((address | 0b1111100011011111) == 0xFFDF) {
      return _mouse->yAxis();
    }
    if ((address | 0b1111100011011111) == 0xFADF) {
      return _mouse->buttons();
    }

    return 0xFF;
  }

#if PICO_RP2350
  // diag6: newest-first overwrite ring, one entry per TIM-mode port write
  // (stores the port LOW byte in port_hi and the written value byte).
  inline void timsOpPush(uint16_t address, uint8_t value) {
    volatile TimsWriteOp *e = &_opRing[_opIdx & 15];
    e->port_hi = (uint8_t)(address & 0xFF);
    e->val = value;
    ++_opIdx;
  }
#endif

#ifdef ZX_PLUS3_ROM_AVAILABLE
  // diag10: newest-first overwrite ring push, one entry per +3s port event
  // (isRead: 0 = OUT, 1 = IN with the value the CPU got back).
  inline void plus3sOpPush(uint16_t port, uint8_t value, uint8_t isRead) {
    volatile Plus3sPortOp *e = &_s3Ops[_s3Idx & 255];
    e->port = port;
    e->val = value;
    e->isRead = isRead;
    ++_s3Idx;
  }
#endif

inline void writeIO(uint16_t address, uint8_t value)
  {
    if (_type == ZxSpectrumCobra) {
      cobraWriteIO(address, value);
      return;
    }
    if (_type == ZxSpectrumCubz) {
      cubzWriteIO(address, value);
      return;
    }
#if PICO_RP2350
    // ZX48 SuperSpec writes (phase 2): the GAL cfwrite strobe decodes like
    // cfread (low byte A7=0, A3=1 = 0x??7F, EPROM bank latch in the 224
    // family) - command 0x20/0x30 at 0xC17F, sector count/LBA registers and
    // the 512B data stream through 0x007F. Without this hook the DOS's OUTs
    // fell through into the AY ($C17F & $C002 == $C000 -> writeCtrl!) and
    // the ULA border handler (0x007F is even). Odd unmatched ports are open
    // bus (no AY on the SuperSpec board); even ports fall through to the
    // standard ULA $FE border/speaker handler below.
    if (_type == ZxSpectrumSuperSpec) {
      const uint8_t a = (uint8_t)address;
      if ((a & 0x88) == 0x08) {
        if (_ssCf && (_ssEpBank & 3) == 0) _ssCf->writeReg(zxSuperSpecCfRegIndex(address), value);
        return;
      }
      if (address & 0x0001) return;
      // even ports fall through to the ULA border/speaker handler below
    }
#endif
#if PICO_RP2350
    // TIM-S Plus register panel (fuse tims_plus_io_write). Every decoded
    // port write re-applies the memory map. The FDC $3FFD data port and the
    // $5CFD scroll latch are latch-only; $1FFD bit 3 is the external FDC
    // motor line (fuse tims_fdc_motor: both drives follow the signal
    // directly, no monostable). Unmatched odd ports ($FFD/$BFFD) are
    // undecoded open bus. Even ports ($FE border) fall through below.
    if (_type == ZxSpectrumTimsPlus && (address & 0x0001)) {
      switch (address) {
        case 0x7FFD:
          _timsPaging = value; break; // B5 is not a Spectrum paging lock
        case 0x1FFD:
          _timsSecondary = value; // bit3: FDC motor line (fuse tims_fdc_motor)
          _hcFdc.setTimsMotor((_timsSecondary & 8) != 0);
          break;
        case 0x2FFD:
          break; // main status register is read-only
        case 0x3FFD:
#ifdef COBRA_FDC_TRACE
          printf("TIMS FDC <- %02X PC=%04X\n", value, Z80_PC(_Z80));
#endif
          _hcFdc.writeDataPc(Z80_PC(_Z80), value); break;
        case 0x0CFD:
          if (!(_timsControl & 9)) {
            _timsPc = value;
          }
          break;
        case 0x0EFD:
          if (!(_timsControl & 16)) {
            _timsPa = value;
          }
          break;
        case 0x0FFD:
          if (!(_timsControl & 2)) {
            _timsPb = value;
          }
          break;
        case 0x5CFD:
          _timsScroll = value; break;
        case 0x0DFD:
          // 8255 US: control word MSB set = mode set clears the output
          // latches; otherwise single-bit set/reset of port C. Same pattern
          // as the CoBra U79 (cobraWriteIO).
          if (value & 128) {
            _timsControl = value;
            _timsPc = _timsPa = _timsPb = 0;
          }
          else {
            const uint8_t m = 1 << ((value >> 1) & 7);
            if (value & 1) _timsPc |= m; else _timsPc &= ~m;
          }
          break;
        default:
          // diag6: unmatched odd ports are open bus, but the attempted
          // write is still a TIM-type port write for the trace ring.
          timsOpPush(address, value);
          return;
      }
      applyTimsMap();
      // diag6: every TIM-mode latch write goes through this exit
      timsOpPush(address, value);
      return;
    }
#endif
    // HC88 odd-port writes (fuse hc88.c:103-112): the i8272 ports use an
    // exact low-byte decode (periph mask 0x00FF, machines_periph.c:258-260)
    // - $DF is the FDC data (write) port, $E7 the control latch
    // (TC/motor/reset/drive select via setHc88Latch()). $DD (MSR) is
    // read-only. The motherboard latch is partially decoded by
    // A7,A6,A0 = 1,0,1 ((port & 0xC1) == 0x81, also machines_periph.c:262)
    // and MUST be tested after the $E7 exact decode, since $E7 also matches
    // the (port & 0xC1) == 0x81 partial decode on the board. Any other odd
    // write is open bus. Even ports fall through to the standard ULA $FE
    // handler below (border/keyboard/speaker).
    if (_type == ZxSpectrumHc88 && (address & 0x0001)) {
      switch (address & 0x00FF) { // exact low-byte decode, fuse mask 0x00ff
        case 0x00DF: _hcFdc.writeDataPc(Z80_PC(_Z80), value); return;
        case 0x00E7: _hcFdc.setHc88Latch(value); return;
        default: break; // $DD and everything else: fuse switch default case
      }
      if ((address & 0xC1) == 0x81) { // partial decode, fuse hc88.c:109 - exact $DD also lands here (0xDD & 0xC1 == 0x81)
        ++_hc88LatchWr; // diag: motherboard latch-port write
        _hc88Latch = value;
        _hc88LatchLast = value; // diag: last latch value written
        applyHc88Map();
      }
      return;
    }
    if (_type == ZxSpectrumHc2000) {
      // HC2000: every OUT to a port with A0=0 and A7=0 writes the port 7E
      // latch (D0 = 16K ROM half, D1 = map ROM at $E000-$FFFF, D2 = lock).
      if (!(address & 0x0081)) {
        if (!(_hcLatch7e & 4)) {
          _hcLatch7e = value & 0x0F;
          applyHc2000Map();
        }
        return;
      }
      // HC2000: port C5/C7 selects the CP/M bank flag from address bit A1
      // (C5 = off, C7 = on) regardless of the written value; the ROM always
      // writes 0xEE (HcIoProfile.Faithful: SetCpm((port & 2) != 0)).
      if ((address & 0x00FD) == 0x00C5) {
        _hcCpm = (address & 2) != 0;
        applyHc2000Map();
        return;
      }
      // HC2000: 05/H drive latch (odd ports with A2=0, A5=0)
      if ((address & 0x00FD) == 0x0005) {
        _hcFdc.writeLatch(value);
        return;
      }
      // HC2000: 87/H FDC data register (even, A1=1, A2=1, A7=1)
      if ((address & 0x00FF) == 0x0087) {
        _hcFdc.writeDataPc(Z80_PC(_Z80), value);
        return;
      }
	  } else if (_type == ZxSpectrumPlus3) {
      if ((address & 0xF002) == 0x1000) {
        if ((_portMem & 0x20) == 0) {
          _plus3Latch1ffd = value;
          applyPlus3Map();
          _hcFdc.setPlus3Motor((value & 8) != 0);
        }
        return;
      }
      if ((address & 0xF002) == 0x2000) return; // main status register is read-only
      if ((address & 0xF002) == 0x3000) {
        _hcFdc.writeDataPc(Z80_PC(_Z80), value);
        return;
      }
#ifdef ZX_PLUS3_ROM_AVAILABLE
	  } else if (_type == ZxSpectrumPlus3s) {
      // Same paging/FDC ports as the plain +3, driven by applyPlus3sMap()
      // over the +3s ROM set. $7F7E bit5 is the +3s 48K paging lock (cs port
      // tables); it is stored in the latch for readback fidelity but does NOT
      // gate the paging port writes (hardware log L3: Secară's loader does
      // IN-modify-OUT cycles whose restore writes carry bit5=1 - a write gate
      // would drop every later 7FFD/1FFD write and the loader's shadow
      // activation would never survive).
      if ((address & 0xF002) == 0x1000) {
        // diag10: log every 1FFD OUT attempt - the latch sequence is what
        // the hang analysis needs to see.
        plus3sOpPush(address, value, 0);
        // No write gate: every OUT applies (latch value stored faithfully,
        // bit5 included). The $7F7E bit5 48K-lock stays latch-only for now.
        _plus3Latch1ffd = value;
        applyPlus3sMap();
        _hcFdc.setPlus3Motor((value & 8) != 0);
        return;
      }
      if ((address & 0xF002) == 0x2000) return; // main status register is read-only
      if ((address & 0xF002) == 0x3000) {
        _hcFdc.writeDataPc(Z80_PC(_Z80), value);
        return;
      }
      // Custom motherboard ports (NEWEPR.GEN / cs port tables): exact 16-bit
      // decode. Phase 2a: the $7F7E (shadow ROM + write gate + 48K lock) and
      // $DF7E (extended RAM at $C000) writes re-apply the memory map; $BF7E
      // (VRAM control) and $EF7E (Kempston enable + clock, report-only) stay
      // latch-only. Writes on any other machine type never reach here.
      switch (address) {
        // diag10: every motherboard-latch OUT attempt goes into the ring.
        case 0x7F7E: plus3sOpPush(address, value, 0); _plus3sLatch[0] = value; applyPlus3sMap(); return;
        case 0xBF7E: plus3sOpPush(address, value, 0); _plus3sLatch[1] = value; return;
        case 0xDF7E: plus3sOpPush(address, value, 0); _plus3sLatch[2] = value; applyPlus3sMap(); return;
        case 0xEF7E: plus3sOpPush(address, value, 0); _plus3sLatch[3] = value; return;
      }
#endif
	  }
	  if (address & 0x0001)
	  {
      if ((address & 0xC002) == 0xC000)
	  	{ 
		  	_ay.writeCtrl(value);
			  return;
  		} //FFFD
		
      if ((address & 0xC002) == 0x8000) 
		  { 
			  _ay.writeData(value);
  			return;
	  	} //BFFD
		
      if (!(address & 0x8002) && (_type == ZxSpectrum128k || _type == ZxSpectrumPlus3 ||
          _type == ZxSpectrumPlus3s))
  		{
        // diag10: log the 7FFD OUT attempt on the +3s (every write now
        // applies, see the phase 2a note below).
        if (_type == ZxSpectrumPlus3s) plus3sOpPush(address, value, 0);
        if (_type == ZxSpectrumPlus3s) {
          // +3s (hardware log L3 fix): NO write gate on the paging latch -
          // the loader's IN-modify-OUT restore writes carry bit5=1
          // (7FFD=0xFE), so gating on _portMem bit5 (the plain +3 lock) or
          // $7F7E bit5 would drop all subsequent 7FFD writes and the shadow
          // activation would never survive. Every OUT applies; bit5 is still
          // stored faithfully for the IN #7FFD readback. The $7F7E bit5
          // 48K-lock is latch-only for now (no 48K restriction applied).
          _fc += ((_portMem ^ value) >> 3) & 1;
          _portMem = value;
          applyPlus3sMap();
        } else if ((_portMem & 0x20) == 0) {
          // Plain 128K/+3: bit5 of the latch is the paging lock ("reading
          // #7FFD writes #FF" quirk) - while set further writes are ignored.
          _fc += ((_portMem ^ value) >> 3) & 1;
          _portMem = value;
          if (_type == ZxSpectrumPlus3) applyPlus3Map();
          else {
            setPageaddr(3, (uint8_t*)&_RAM[value & 7]);
            setPageaddr(0, (uint8_t*)((value & 0x10) ? zx_128k_rom_2 : zx_128k_rom_1));
          }
        }
			  return;
		  };
  	}
	else
	    {
        _port254 = value;
        _borderColour = value & 7;
        // Turbo Spectrum: any OUT to an even port stores the byte in F5
        if (_type == ZxSpectrumTurbo) _feReg = value;
#if PICO_RP2350
        // diag6: in TIM mode $FE border writes reach this even-port handler
        // (they do not enter the odd-port latch section above); count them
        // and push the trace entry here too.
        if (_type == ZxSpectrumTimsPlus) {
          ++_brWr;
          timsOpPush(address, value);
        }
#endif
      }
  }

  static uint8_t __not_in_flash_func(readByte)(void * context, uint16_t address) {
    return ((ZxSpectrum*)context)->readByte(address);
  }

  // HC2000: the CP/M loader turns the Interface I shadow on/off at fixed
  // fetch addresses (soft reset $0008 / $1708 on, $0700 off).
  // HC88: the IF1 shadow-RAM latch (U37A) is armed by M1 fetches in
  // $0008-$000F / $1708-$170F and released by M1 fetches in $0700-$0707
  // (fuse hc88.c:114-124 decode M1 ranges, not an external IF1 ROM). Only
  // active with b0 set (RAM at Q0); arming/remap applies immediately so the
  // current fetch already reads the new map, following the same order the
  // HC2000 hook uses here. Implemented through the same hcFetchOpcode hook.
  inline uint8_t readOpcode(uint16_t address) {
    if (_type == ZxSpectrumHc2000 && (address == 0x0008 || address == 0x1708))
      _hcIf1Shadow = true;
    else if (_type == ZxSpectrumHc88 && (_hc88Latch & 1) && !_hc88If1Paged &&
             ((address & 0xFFF8) == 0x0008 || (address & 0xFFF8) == 0x1708)) {
      _hc88If1Paged = true;
      applyHc88Map();
    }
    const uint8_t value = readByte(address);
    if (_type == ZxSpectrumHc2000 && address == 0x0700)
      _hcIf1Shadow = false;
    else if (_type == ZxSpectrumHc88 && _hc88If1Paged &&
             (address & 0xFFF8) == 0x0700) {
      _hc88If1Paged = false;
      applyHc88Map();
    }
    return value;
  }

  static uint8_t __not_in_flash_func(hcFetchOpcode)(void * context, uint16_t address) {
    return ((ZxSpectrum*)context)->readOpcode(address);
  }
  
  static void __not_in_flash_func(writeByte)(void * context, uint16_t address, uint8_t value) {
    ((ZxSpectrum*)context)->writeByte(address, value);
  }
   
  static uint8_t __not_in_flash_func(readIO)(void * context, uint16_t address) {
    return ((ZxSpectrum*)context)->readIO(address);
  }

  static void __not_in_flash_func(writeIO)(void * context, uint16_t address, uint8_t value) {
    ((ZxSpectrum*)context)->writeIO(address, value);
  }
  
  static uint8_t __not_in_flash_func(readInt)(void * context, uint16_t address) {
    ZxSpectrum *self = (ZxSpectrum*)context;
    if (self->_type == ZxSpectrumCobra) {
      // IM2 vector from the floppy interface CTC; nothing else drives the
      // bus, so the frame interrupt reads FF (R10-R17 pull-ups).
      const uint8_t v = self->_cobraFloppy.intLine() ? self->_cobraFloppy.acknowledge() : 0xFF;
      z80_int(&self->_Z80, self->_cobraFloppy.intLine());
      return v;
    }
    z80_int(&self->_Z80, false);
    return 0xff;
  }

  // CoBra: an M1 cycle has just fetched its opcode; the refresh that follows
  // puts R on the address bus and U36A latches A7 (R7) into PO.  PO is
  // cleared while LO6=0, preset while NPOR is active, and its 1->0 edge
  // clocks O6 into LO6.
  inline void cobraRefresh() {
    const bool po = _cobraPor > 0 || (_cobraLO6 && (_Z80.r7 & 0x80));
    if (po == _cobraPO) return;
    _cobraPO = po;
    if (!po) _cobraLO6 = (cobraPortC() & 0x40) != 0;
    cobraMap();
  }

  static uint8_t __not_in_flash_func(cobraFetchOpcode)(void * context, uint16_t address) {
    ZxSpectrum *self = (ZxSpectrum*)context;
    const uint8_t v = self->readByte(address);
    self->cobraRefresh();
    return v;
  }

  static void __not_in_flash_func(cobraReti)(void * context) {
    ZxSpectrum *self = (ZxSpectrum*)context;
    if (self->_type != ZxSpectrumCobra) return;
    self->_cobraFloppy.reti();
    z80_int(&self->_Z80, self->_cobraFloppy.intLine());
  }

  // 8255 port C as seen by the hardware: bits programmed as inputs float high.
  inline uint8_t cobraPortC() {
    if (_cobraPor > 0) return 0xFF;
    const uint8_t in = ((_cobraPpiCtrl & 0x08) ? 0xF0 : 0) | ((_cobraPpiCtrl & 0x01) ? 0x0F : 0);
    return (_cobraPortC & ~in) | in;
  }

  void cobraMap();
  void cubzMap();
  uint8_t cubzReadIO(uint16_t address);
  void cubzWriteIO(uint16_t address, uint8_t value);
  void cobraPortChanged();
  uint8_t cobraReadIO(uint16_t address);
  void cobraWriteIO(uint16_t address, uint8_t value);
  
  // _RAM pool bank that stores the custom 48K ROM image (see _customRom).
  // Highest bank of the 8-bank pool; never paged in by the 48K machine. On
  // the 128K/+3/+3s machines this bank doubles as the SHADOW SCREEN (7FFD
  // bit3) and on the HC2000 it holds the IF1 shadow ROM, so the menu custom-
  // ROM load is guarded to the 48K machine only (diag9 aliasing fix).
  enum { CustomRomBank = 7 };

  uint8_t _RAM[8][1<<14];
  // Custom 16K ROM image for the 48K machine (menu option R): bank 7 of the
  // shared _RAM pool above (pool offset _RAM + 0x1C000). No heap allocation
  // and no extra static buffer - the image lives in a pool bank the 48K
  // machine never touches, so it cannot steal RAM from the heap (the old
  // 16K static member shifted/shrank the scanvideo render-buffer heap and
  // blacked the auto VGA picture). Null pointer by default -> the built-in
  // flash ROM is used. No persistence across power cycles (the pointer is a
  // plain member, never saved). Intentionally NOT freed or cleared on a 48K
  // reset: a reload-less "Reset 48K" keeps booting the same custom ROM within
  // the session (reset() skips zeroing the bank while the 48K machine uses
  // it). Bank 7 is idle while the 48K machine runs: its CPU pages are
  // Q0=ROM/custom buffer, Q1=_RAM[5] (screen), Q2=_RAM[2], Q3=_RAM[0], and
  // port $7FFD paging exists on the 128K/+3 machines only. The 128K/+3
  // machines page bank 7 into Q3 (and HC2000 keeps its IF1 shadow in _RAM[7]),
  // so transmute() invalidates the custom ROM whenever a non-48K machine is
  // selected - a custom ROM must be re-selected after switching back to the
  // 48K machine (menu row keeps the file name but the image is dropped).
  uint8_t* _customRom = 0;
#if PICO_RP2350
  // TIM-S Plus memory (boot-only phase, RP2350 only): 12 x 16K system RAM
  // banks (fuse pages 0-11, 192K) plus the separate 16K video RAM (fuse page
  // 12; 8K plaster halves 24/25 in the CPU-selectable window). Kept in a
  // separate pool under PICO_RP2350 so RP2040 builds are byte-for-byte
  // unchanged - the menu entry and all TIM-S code paths compile out.
  uint8_t _timsRAM[12][1<<14];
  uint8_t _timsVram[1<<14];
#endif

  void transmute(ZxSpectrumType type);
  void applyTurboMap();
  void applyHc2000Map();
  void applyPlus3Map();
  void applyPlus3sMap();
#if PICO_RP2350
  void applyTimsMap();
#endif
  void applyHc88Map();
#if PICO_RP2350
  void applySuperSpecMap();
#endif
  // 48K images run on the TURBO-SPECTRUM clone while that machine is selected
  inline ZxSpectrumType type48(ZxSpectrumType type) {
    return (type == ZxSpectrum48k && _type == ZxSpectrumTurbo) ? ZxSpectrumTurbo : type;
  }
  int32_t loadZ80MemV0(InputStream *is);
  int32_t loadZ80MemV1(InputStream *is);
  int32_t loadZ80Header(InputStream *is);
  int32_t loadZ80HeaderV2(InputStream *is, ZxSpectrumType *type);
  int32_t loadZ80MemBlock(InputStream *is, const ZxSpectrumType type);
  int32_t writeZ80Header(OutputStream *os, int32_t version);
  int32_t writeZ80HeaderV3(OutputStream *os);
  int32_t writeZ80(OutputStream *os, int32_t version);
  int32_t writeZ80MemV0(OutputStream *os);
  int32_t writeZ80MemV1(OutputStream *os);
  int32_t writeZ80MemV2(OutputStream *os, uint8_t *blk);
  int32_t writeZ80MemV2(OutputStream *os, const uint8_t b, const uint8_t p);
  int32_t writeZ80MemV2(OutputStream *os);
  int32_t z80BlockToPage(const uint8_t b, const ZxSpectrumType type);
  
  inline int32_t getBuzzerSmoothed() {
    const int32_t a1 = (_buzzer >> 4) - (_buzzerl >> 12) ;
    const int32_t a2 = a1 < -112 ? -112 : a1 > 111 ? 112 : a1;
    const int32_t a3 = _ear ? -16 : 16;
    return a2 + a3 + 128;
  }
  
  inline uint32_t getBuzzer() {
    return ((_port254 >> 4) ^ _ear) & 1;
  }

  void stepScanline(const uint32_t c);

  inline void interrupt() {
    _fcf = _fc;
    _fc = 0;
    // CUB-Z: no frame interrupt (its CTC/PIO/SIO use the daisy chain).
    if (_type == ZxSpectrumCubz) return;
    if (_type == ZxSpectrumCobra) {
      // NVS clocks the floppy CTC TRG3 (JP21 1-2).  The frame interrupt
      // (VLD) only reaches INT in Spectrum mode (U17: NLO6).
      _cobraFloppy.frame();
      if (!_cobraLO6) {
        // VLD pulse: the request is latched, the line drops with the pulse.
        z80_int(&_Z80, true);
        _Z80.int_line = _cobraFloppy.intLine();
      }
      else {
        z80_int(&_Z80, _cobraFloppy.intLine());
      }
      return;
    }
    z80_int(&_Z80, true);
    _Z80.int_line = false;
    if (_type == ZxSpectrumHc2000) _hcFdc.frameTick();
  }

public:

  ZxSpectrum(
    ZxSpectrumKeyboard *keyboard1,
    ZxSpectrumKeyboard *keyboard2,
    ZxSpectrumJoystick *joystick,
    ZxSpectrumMouse *mouse
  );

  inline uint8_t* screenPtr() {
    if (_type == ZxSpectrumCobra) return (unsigned char*)&_RAM[5] + (cobraAv13() ? 0x2000 : 0);
#if PICO_RP2350
    // TIM-S Plus raster source, live per frame (diag5):
    // - PC bit4 set (boot EPROM / CP/M Vol-III half): the separate 16K VRAM,
    //   unchanged from diag4.
    // - PC bit4 cleared (the ROM's 8255 mode set at 0xDFD after a disk load):
    //   follow the system bank actually mapped at $4000-$7FFF and resolve the
    //   screen pointer through the live 8K read-page table exactly like the
    //   CPU read path for address 0x4000 (memaddr(): _pageaddr[0x4000>>13]),
    //   so applyTimsMap() result is honoured every frame without snapshotting.
    //   This includes the videoWindow / pages[1]==12 case, where the page
    //   table entry already points at the VRAM 8K half (naturally consistent).
    //   The whole $4000-$5AFF screen (0x1B00 bytes) fits in the first 8K block
    //   of that window.
    // The raster always reads the separate video RAM: in CP/M mode (PC bit4
    // clear) the console is written there through the VRAM window too, so
    // showing the system page mapped at $4000 left the CP/M screen black.
    if (_type == ZxSpectrumTimsPlus) return (uint8_t*)_timsVram;
#endif
    // HC88: the standard 32-col Spectrum raster always reads the separate
    // 16K VRAM (fuse hc88.c:73 memory_current_screen = 4; the video never
    // switches to a main-RAM bank). Bit 4 of the latch (hires) is stored
    // but not decoded this phase.
    if (_type == ZxSpectrumHc88) return (uint8_t*)_hc88Vram;
    // HC2000: VideoBase (HcMemory.cs) follows latch 7E bit 3 — CP/M shows the
    // screen from physical 0xC000 (_RAM[0]), otherwise physical 0x4000 (_RAM[5]).
    if (_type == ZxSpectrumHc2000) return (uint8_t*)&_RAM[(_hcLatch7e & 8) ? 0 : 5];
    return (unsigned char*)&_RAM[(_portMem & 8) ? 7 : 5];
  }
  // CoBra video RAM A13: the 8K half of the 16K video RAM on display.
  inline bool cobraAv13() {
    if (_cobraConfig == Cobra64K) return false;     // JP12 2-3: A13 = GND
    return _cobraLO6 && (cobraPortC() & 0x20) && !_cobra80; // O5 AND LO6
  }
  // CoBra 80 chr/lin module: in CP/M, once O5 is set, AV13 = AC5 - the paper
  // shows the lower half of the video RAM, the horizontal border the upper.
  inline bool cobra80Char() { return _type == ZxSpectrumCobra && _cobra80; }
  // CUB-Z VDT-52S: 512x256 monochrome raster, column-major in the video RAM
  // (byte of column c, line y at c * 256 + y), first line = SCROLL latch.
  inline bool cubz() { return _type == ZxSpectrumCubz; }
  inline uint8_t* cubzVram() { return (unsigned char*)&_RAM[4]; }
  inline uint8_t cubzScroll() { return _cubzScroll; }
  void cubzKey(uint8_t ascii);
  bool cubzInsertDisk(uint8_t drive, CobraDiskMedia *media);
  void cubzEjectDisk(uint8_t drive) { cubzInsertDisk(drive, 0); }
  inline uint8_t* cobraVram() { return (unsigned char*)&_RAM[5]; }
  CobraConfig cobraConfig() { return _cobraConfig; }
  void cobraConfig(CobraConfig config) { _cobraConfig = config; }
  // K1 "RESET BASIC": only the CPU (and the floppy CTC) see NRST.
  void cobraResetBasic();
  bool cobraInsertDisk(uint8_t drive, CobraDiskMedia *media) { return _cobraFloppy.insert(drive, media); }
  void cobraEjectDisk(uint8_t drive) { _cobraFloppy.eject(drive); }
  // ICE Felix HC2000 floppy disk controller direct access (menu disk mounting)
  inline ZxSpectrumHcFdc &hcFdc() { return _hcFdc; }
  // Diag accessors for the TIM drive menu rows (strippable later).
#if PICO_RP2350
  inline uint8_t timsPc() { return _timsPc; }
  inline uint8_t timsSecondary() { return _timsSecondary; }
  inline uint8_t timsPaging() { return _timsPaging; }
  inline uint32_t wrVram() { return _wrVram; }
  // diag6: write-op ring + border-write counter accessors (strippable later).
  inline uint32_t timsBrWr() { return _brWr; }
  inline uint8_t timsOpIdx() { return _opIdx; }
  inline volatile TimsWriteOp* timsOps() { return _opRing; }
  // diag7 phase 2: data-port read ring accessors (strippable later).
  inline uint8_t timsRdIdx() { return _rdIdx; }
  inline volatile uint8_t* timsRds() { return _fdRd; }
  // fix3 phase 2: TIM-mode IO read counter accessors (strippable later).
  inline volatile uint32_t timsRd2ffd() { return _rd2FFD; }
  inline volatile uint32_t timsRdCfd()  { return _rdCFD; }
  inline volatile uint32_t timsRdPb()   { return _rdPB; }
  inline volatile uint32_t timsRdFe()   { return _rdFE; }
#endif
  // diag8: live +3s CPU/paging readback for the menu header row 1 (strippable
  // later). Z80_PC/Z80_SP reach the emulation core register struct (Z80.h);
  // the latch getters return the internal state of the +3s paging latches.
  // NOT platform-gated: ZxSpectrumMenu.cpp prints the s3 line on every
  // machine when the +3s type is selected, and every backing member
  // (_plus3sLatch/_plus3Latch1ffd/_portMem/screenPtr/_Z80) exists on both
  // RP2040 and RP2350.
  inline uint16_t dbgZ80Pc() { return Z80_PC(_Z80); }
  inline uint16_t dbgZ80Sp() { return Z80_SP(_Z80); }
  inline uint8_t dbgPlus3s1ffd() { return _plus3Latch1ffd; }
  inline uint8_t dbgPlus3s7ffd() { return _portMem; }
  inline uint8_t dbgPlus3s7f7e() { return _plus3sLatch[0]; }
  // diag11: DF7E latch (bit3 = C000->shadow override). The loader's EXIT
  // restore path writes this last; a stuck bit3 redirects DOS init to the
  // shadow banks and hangs the boot at the +3DOS page-in.
  inline uint8_t dbgPlus3sDf7e() { return _plus3sLatch[2]; }
  // diag9: live raster screen-page probe (menu row-1 line). The +3s question
  // was "which screen page does the renderer actually read": dbgScreenPage()
  // resolves the live screenPtr() back to the _RAM pool bank index (5 =
  // normal screen, 7 = shadow screen, following 7FFD bit3) and
  // dbgScreenAddr() returns the low 16 bits of the first screen byte
  // pointer (bank 5 and bank 7 differ by 0x8000 in the low 16 bits).
  inline uint8_t dbgScreenPage() {
    return (uint8_t)(((uintptr_t)(void*)screenPtr() - (uintptr_t)(void*)_RAM) >> 14);
  }
  inline uint16_t dbgScreenAddr() { return (uint16_t)(uintptr_t)(void*)screenPtr(); }
#ifdef ZX_PLUS3_ROM_AVAILABLE
  // diag10: +3s port-event ring accessors for the menu diag line
  // (strippable later). Entries are consumed newest-first via
  // (idx-1-n) & 23.
  inline volatile Plus3sPortOp* plus3sOps() { return _s3Ops; }
  inline uint8_t plus3sOpIdx() { return _s3Idx; }
#endif
  // HC88 diag counter accessors (strippable later).
  inline volatile uint32_t hc88LatchWr() { return _hc88LatchWr; }
  inline volatile uint32_t hc88VramWr() { return _hc88VramWr; }
  inline volatile uint32_t hc88Q0Wr() { return _hc88Q0Wr; }
  inline volatile uint8_t hc88LatchLast() { return _hc88LatchLast; }
  inline uint8_t* memPtr(uint32_t i) { return (unsigned char*)&_RAM[i]; }
  // Custom 48K ROM buffer access (menu ROM chooser copies the image here;
  // the buffer is bank 7 of the shared _RAM pool on every board - no heap
  // allocation, no extra static buffer). Only the menu path guarded to the
  // 48K machine may write it: bank 7 is the 128K/+3/+3s shadow screen bank
  // and the HC2000 IF1 shadow while other machine types run.
  inline uint8_t* customRom() {
    if (!_customRom) _customRom = (uint8_t*)&_RAM[CustomRomBank];
    return _customRom;
  }
  inline bool customRomLoaded() { return _customRom != 0; }
  inline void customRomLoaded(bool loaded) {
    if (!loaded) {
      // Invalidate only: bank 7 is left as-is and gets zeroed by the next
      // reset() of any machine (the machine-switch guard runs through
      // transmute(), which fires before reset() zeroes the pool).
      _customRom = 0;
    }
  }
  inline uint32_t flipsPerFrame() { return _fcf; }
  void reset(ZxSpectrumType type);
  ZxSpectrumType type() { return _type; }
  ZxSpectrumIntSource intSource() { return _intSource; }
  void intSource(ZxSpectrumIntSource intSource) { _intSource = intSource; }
    
  uint32_t step();

  inline void vsync() {
    if (_intSource == SyncToDisplay) {
      interrupt();
      _sl = 0; 
      _slc = 0;
    }
  }

  void setAudioDriver(zx_spectrum_audio_driver_t* audio_driver);

  inline zx_spectrum_audio_driver_t* getAudioDriver() {
    return _audio_driver;
  }

  void moderate(uint32_t mul);
  void toggleModerate();
  uint32_t moderate() { return _moderate; }
  void mute(bool mute) { _mute = mute; }
  void toggleMute() { _mute = !_mute; }
  bool mute() { return _mute; }

  inline uint8_t borderColour() { return _borderColour; }
  inline uint8_t borderColour(uint32_t y) { return _borderBuf[y]; }

  void setEar(bool ear) { _ear = ear; }
  bool getEar() { return _ear; }
  void loadZ80(InputStream *inputStream);
  bool saveZ80(OutputStream *outputStream);
  void loadTap(InputStream *inputStream);
  void loadTzx(InputStream *inputStream);
  void ejectTape();
  bool tapePaused();
  void pauseTape(bool pause);
  void togglePauseTape();
  ZxSpectrumJoystick *joystick() { return _joystick; }
  ZxSpectrumKeyboard *keyboard1() { return _keyboard1; }
  ZxSpectrumKeyboard *keyboard2() { return _keyboard2; }
  ZxSpectrumMouse *mouse() { return _mouse; }
  
  void tzxOptionHandlers(
    std::function<void()> clearOptions,
    std::function<void(const char *)> addOption,
    std::function<void()> showOptions
  ) {
    _pulseChain.optionHandlers(
      clearOptions,
      addOption,
      showOptions
    );
  }
  void tzxOption(uint32_t option) { _pulseChain.option(option); }

  void toggleIntSource() { 
    switch(_intSource) {
      case SyncToCpu: _intSource = SyncToDisplay; break;
      default: _intSource = SyncToCpu; break;
    }
   }
};
