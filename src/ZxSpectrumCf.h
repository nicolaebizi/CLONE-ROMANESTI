// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#pragma once

#include <stdint.h>
#include "pico.h"
#include "SdCardFatFsSpi.h"
#include "ff.h"

// Multi-sector transfer buffer size (sectors): 8 = 4K everywhere. The BSS
// budget (scanvideo frame buffers + heap) cannot afford a 32K transfer
// chunk; 8-sector chunks keep the chunked f_read/f_write state machine
// unchanged with barely-measurable SPI-SD throughput impact.
#define CF_BUF_SECTS 8

// Uniform-write table capacity: one entry per recorded uniform sector run.
// Koning's DIR blocks are CONTIGUOUS LBA ranges (DIR #n ends exactly where
// #n+1 starts), so the FORMAT!'s uniform clears coalesce into ONE giant run
// and a handful of entries suffice; 64 entries also absorb a moderately
// fragmented pattern of 1-sector uniform writes (a table-full uniform write
// still degrades safely to a real f_write). 8 bytes/entry = 512 B BSS.
#define CF_UWT_MAX 64

// ZX48 SuperSpec CompactFlash in MEMORY MODE (phase 2).
//
// R. Koning's board wires the CF card 8-bit "memory mode" style: /CE1 tied
// low, /OE <- GAL cfread, /WE <- GAL cfwrite, and the CF register address
// lines A0/A1/A2 driven from Z80 A8/A14/A15 - the register index comes from
// the HIGH byte of the port address (port table, roeloef/
// STUDIU_EMULARE_ROELOF.md §4.2 + Myflashdisk-all.rtf):
//   0x007F DATA (even, streams)   0x017F DATA-odd (peek, no advance)
//   0x407F SECTOR COUNT           0x417F LBA 0-7
//   0x807F LBA 8-15               0x817F LBA 16-23
//   0xC07F LBA 24-27 (|0xE0)      0xC17F STATUS / COMMAND
// Commands: 0x20 READ SECTORS, 0x30 WRITE SECTORS (DOS source
// 224DosRom201415.rtf getsecthl2/stosecthl2).
//
// The backing store is a raw 512B-sector image file on the emulator's own SD
// card, opened lazily at the first CF access (a missing file is auto-created
// blank and grown to 3,670,000,000 bytes with METADATA-ONLY FatFs calls -
// f_lseek past EOF stretches the cluster chain, no data writes, no
// contiguity needed - Koning's FS is ROOT at LBA 1024 + 1000 DIR blocks of
// 7168 sectors each = ~3.5 GB of structure, and a 16 MB image made every
// read past its end fail, which the ROM turned into its "press a key"
// error loop; a real CF card is sized for
// the whole structure and its controller makes FORMAT! instant). The 0xFF
// (erased state) content is never written to the SD: it lives in the
// UNIFORM-WRITE TABLE below. On which the ROM's own FORMAT! command builds
// the superDOS structure; no prepared dump needed. Data flows through a
// single 512B buffer; reg 0 advances the buffer
// pointer on every read/write (in 8-bit memory mode each /OE or /WE strobe
// moves one byte - the DOS reads two consecutive bytes from port 127 all
// over its source, e.g. fetch4/fetchsinc7/movimembot), reg 1 peeks the
// current byte without advancing (the "data-odd" behaviour: the next byte
// can be read twice from reg 0).
//
// UNIFORM-WRITE TABLE (virtual-format overlay): a real CF controller makes
// Koning's FORMAT! fast because clearing 1000 DIR blocks of 7168 sectors is
// just a NAND erase; on our write-through SPI-SD image it used to be hours
// of 0x20-sector writes. Now a completed write whose sectors are ALL one
// byte value (0x20 DIR clears, 0xFF erases) is NOT written to the SD at all:
// it is recorded as a (startLba, count, val) range in a compact in-RAM
// table, coalescing with the previous adjacent same-value range. Reads
// resolve table hits first (fabricated pattern, no SD access); misses read
// the file as before; LBAs beyond the file size read 0xFF (erased state)
// instead of failing. Real (non-uniform) writes go through f_write and
// carve the recorded ranges they overlap into two, so the real data always
// wins. The table is image content, not session state: it survives a
// machine reset (the image file does too) and is dropped only when a file
// of a different size is opened.
//
// Status register (0xC17F read): 0x50 idle (BUSY=0, DRDY=1, bit3=0),
// 0x58 data ready (bit3 = 1 - the DOS's tstmult polls BIT 3,A as "more data
// to read", and CFgivcom waits for the exact value 0x58 with CP %01011000),
// 0x01 ERR (no media / write failure / write-protected image).
//
// Write-back strategy (fix5, phase 2): the whole multi-sector transfer is
// buffered in RAM (up to CF_BUF_SECTS sectors per buffer fill) and pushed to
// the image with ONE f_lseek per command, sequential f_write chunk appends
// (no f_sync between chunks) and a single f_sync at the end of the command.
// The DOS issues count=1 write commands (stsect, supr190712.bin 0x1371) and
// loops one command per sector (FORMAT ROOT wipes 1000 DIR sectors this
// way), so the per-command f_sync is already the minimum the DOS asks for;
// the old per-sector f_lseek+f_write+f_sync did up to 3x more I/O per
// sector. Multi-sector read commands (LOAD, prepmult 0x1CDD in this build)
// preload the buffer with one f_read per chunk instead of one per sector.
// Blank-image creation progress mirrors, updated by core0 (ZxSpectrumCf) and
// read by core1's VGA render loop (ZxScanlineVgaRenderLoop) so the 16MB SD
// stall shows an on-screen progress overlay instead of a frozen picture.
// The diag CfDiag keeps the same data for the menu line; these globals exist
// because the menu cannot repaint while core0 is blocked in createBlankImage().
extern volatile uint32_t zxCfImgMaking; // 0 = idle, 1 = blank image being written
extern volatile uint32_t zxCfImgDone;   // bytes of BLANK_IMAGE_BYTES written so far

class ZxSpectrumCf {
public:
  // Raw CF image (LBA*512 offsets) on the emulator's SD card, next to the
  // disks/ folders. Created as a folder by ZxSpectrumMenu::initialise(); the
  // image file itself is auto-created blank (BLANK_IMAGE_BYTES of 0xFF) when
  // missing, and can be replaced by a raw dd dump of the real CF card.
  static const char* IMAGE_PATH;

  // Size of the auto-created image: 3,670,000,000 bytes = 7,167,968.75
  // sectors - the whole Koning FS structure (~3.5 GB: ROOT + 1000 DIR
  // blocks of 3580K each) plus slack, grown with metadata-only
  // f_lseek(end)+f_truncate (no data-sector writes, no contiguity needed).
  // BLANK_IMAGE_BYTES below is only the fallback size for cards too small
  // or too full for the big file (the f_size sanity check catches it).
  static const uint64_t IMAGE_BYTES;

  // Fallback auto-created image size: 16 MB (32768 sectors), byte value
  // 0xFF (erased-NAND state). With the uniform-write table carrying the
  // FORMAT! structure virtually, even this small file boots the DOS.
  // (Reached only when the 3.67 GB metadata growth fails its f_size sanity
  // check - disk full or too small; the diag E counter latches it.)
  static const uint32_t BLANK_IMAGE_BYTES;

  enum : uint8_t { CF_ST_IDLE = 0x50, CF_ST_DRQ = 0x58, CF_ST_ERR = 0x01 };
  enum : uint8_t { CMD_READ = 0x20, CMD_WRITE = 0x30 };

  // Live state snapshot for the menu diag line ("the user sees where it
  // sticks"): the registers latched by the last command, the current status,
  // and per-reset counters of the CF traffic. imgMaking/imgDone report the
  // one-time blank-image creation, which on a slow SPI SD card is the
  // multi-second (up to multi-minute) stall that looks like a hang.
  struct CfDiag {
    uint32_t lba;      // LBA latched by the last command
    uint8_t count;     // COUNT register latched by the last command
    uint8_t status;    // STATUS register now
    uint8_t cmd;       // last command byte (0x20 read / 0x30 write)
    uint32_t nCmd;     // commands since machine reset
    uint32_t nWr;      // bytes written in the CURRENT command (reset when a command is armed)
    uint32_t nRd;      // sectors read since machine reset
    uint32_t nErr;     // ERR status latches seen since machine reset
    uint32_t nUwt;     // uniform-write table entries in use (diag FT=)
    bool imgMaking;    // image creation running
    uint32_t imgDone;  // bytes of image preallocated/written so far
  };

  ZxSpectrumCf(SdCardFatFsSpi* sdCard);


  // Machine reset: register state cleared, the image file handle released so
  // a later reset picks up a freshly copied SUPERSPEC.CF.
  void reset();

  // Register access (index 0..7, from zxSuperSpecCfRegIndex()).
  uint8_t readReg(uint8_t idx);
  void writeReg(uint8_t idx, uint8_t value);

  bool opened() const { return _open; }
  bool writable() const { return _writable; }

  // Copy of the live diag state for the menu line.
  void diag(CfDiag* d) const;

private:
  // Uniform-write table (virtual-format overlay, see the header comment):
  // one recorded uniform sector run per entry.
  struct CfUwtEntry {
    uint32_t startLba; // first LBA of the run
    uint16_t count;    // run length in sectors (max 65535)
    uint8_t val;       // the uniform byte value (e.g. 0x20 DIR clear)
    uint8_t used;      // 0 = free slot
  };
  CfUwtEntry _uwt[CF_UWT_MAX];
  uint16_t _uwtN;      // entries appended so far (used slots may be sparse)
  // Backing-file size (f_size at open): the read/write bounds. Reads beyond
  // it fabricate 0xFF; write commands starting beyond it latch ERR (real CF
  // behaviour for LBA beyond the geometry).
  uint64_t _imgBytes;
  bool _cmdDirty;      // a physical f_write happened in the current command
  bool ensureOpen();
  bool createBlankImage();
  bool createSmallImage();
  void noteImageSize();
  void issue(uint8_t cmd);
  bool loadChunk();
  bool writeChunk();
  bool flushFinal();
  uint8_t dataRead();
  uint8_t dataPeek();
  void dataWrite(uint8_t value);
  // Uniform-write table helpers. uwtAppend records/extends a uniform run
  // (false = table full -> caller falls back to a real f_write);
  // uwtInvalidate carves real-write overlaps out of the table; uwtLookup
  // returns the fabricated byte value for an LBA (-1 = miss, read the
  // file); uwtWriteOut persists a lost range when a split cannot be kept
  // virtual (table full) so reads stay correct.
  bool uwtAppend(uint32_t startLba, uint32_t count, uint8_t val);
  void uwtInvalidate(uint32_t startLba, uint32_t count);
  int uwtLookup(uint32_t lba) const;
  void uwtWriteOut(uint32_t startLba, uint32_t count, uint8_t val);
  // Latch ERR (reg 7 reads 0x01) + count it for the diag E counter (only
  // fresh latches count: ensureOpen re-polls while ERR must not inflate E).
  void fail() { if (_status != CF_ST_ERR) ++_nErr; _status = CF_ST_ERR; }
  uint8_t regRead(uint8_t idx);
  void regWrite(uint8_t idx, uint8_t value);

  SdCardFatFsSpi* _sdCard;
  FIL _fil;
  bool _open;
  bool _writable;
  // Multi-sector transfer buffer: one chunk of the current command's sector
  // range. On reads it holds the next sectors to stream; on writes it holds
  // sectors pending their (chunked) f_write. 8 sectors = 4K (BSS budget:
  // scanvideo frame buffers + heap outrank transfer chunk size).
  uint8_t _buf[CF_BUF_SECTS * 512];
  uint16_t _idx;      // byte index of the current sector into _buf
  uint16_t _bufSects; // sectors valid/pending in _buf
  uint16_t _sectLeft; // sectors still to stream after the current one
  uint32_t _lbaReg;   // LBA28 register latch (regs 3..6)
  uint32_t _curLba;   // LBA of the sector currently in _buf
  uint32_t _chunkEnd; // LBA just past the buffered chunk (reads)
  uint8_t _countReg;  // SECTOR COUNT register latch (reg 2)
  uint8_t _status;    // CF status register (reg 7 read)
  // Diag counters (per machine reset, mirrored to CfDiag on request).
  uint8_t _diagCmd;
  uint32_t _diagLba;
  uint32_t _diagCount;
  uint32_t _nCmd;
  uint32_t _nWr;      // bytes of the current write command (reset in issue())
  uint32_t _nRd;
  uint32_t _nErr;     // ERR latches (diag E counter)
  bool _imgMaking;
  uint32_t _imgDone;
};

// CF register index from the port address high half: A8 -> CF A0 (bit0),
// A14 -> CF A1 (bit1), A15 -> CF A2 (bit2).
inline uint8_t zxSuperSpecCfRegIndex(uint16_t address) {
  return (uint8_t)(((address >> 8) & 1) | ((address >> 13) & 6));
}
