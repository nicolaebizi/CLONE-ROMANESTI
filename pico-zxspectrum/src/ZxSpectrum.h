// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#pragma once
#pragma GCC optimize ("O2")

#include "Z80.h"
#include "hc2000_rom.h"
#include "ZxSpectrumHcFdc.h"
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
  // TIM S Plus (7th computer; boot-only phase, menu entry + transmute/reset
  // arms compile only under PICO_RP2350 so RP2040 builds are unchanged)
  ZxSpectrumTimsPlus,
  // ICE Felix CUB-Z (8th computer): CP/M machine, 64K RAM + 16K video RAM
  ZxSpectrumCubz
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
  volatile uint32_t _rdFE   = 0; // $FE (ULA keyboard) reads
#endif
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
      const bool plus3AllRam = _type == ZxSpectrumPlus3 && (_plus3Latch1ffd & 0x01);
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
    if (address == 0xFFFD) {
      return _ay.readData();
    }else
    if (address == 0x7FFD && (_type == ZxSpectrum128k || _type == ZxSpectrumPlus3)) {
      // reading #7FFD port is the same as writing #FF into it.
      uint8_t value = 0xFF;
      if ((_portMem & 0x20) == 0) {
        _portMem = value;
        if (_type == ZxSpectrumPlus3) applyPlus3Map();
        else {
          setPageaddr(3, (uint8_t*)&_RAM[value & 7]);
          setPageaddr(0, (uint8_t*)((value & 0x10) ? zx_128k_rom_2 : zx_128k_rom_1));
        }
      }
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
		
      if (!(address & 0x8002) && (_type == ZxSpectrum128k || _type == ZxSpectrumPlus3))
   		{
        if ((_portMem & 0x20) == 0) {
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
  inline uint8_t readOpcode(uint16_t address) {
    if (_type == ZxSpectrumHc2000 && (address == 0x0008 || address == 0x1708))
      _hcIf1Shadow = true;
    const uint8_t value = readByte(address);
    if (_type == ZxSpectrumHc2000 && address == 0x0700)
      _hcIf1Shadow = false;
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
  
  uint8_t _RAM[8][1<<14];
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
#if PICO_RP2350
  void applyTimsMap();
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
  inline uint8_t* memPtr(uint32_t i) { return (unsigned char*)&_RAM[i]; }
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
