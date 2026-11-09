// SPDX-License-Identifier: MIT
// ICE Felix HC2000 i8272 floppy disk controller, clean port of the reference
// HC2000 emulator (Fdc8272.cs / FloppyController.cs / FloppyDrive.cs).
//
// Polled non-DMA 8272: RDY/DACK tied high, INT/DRQ never reach the CPU. The
// reference drives every transfer one byte per status/data access ("instant"
// profile, used by the real BIOS), which is what we mirror exactly.
//
// Port wiring (HcIoProfile.Faithful, mask vs low byte):
//   05/H (mask 0xFD)  read/write  drive latch (b3 555 motor trigger,
//                     b4'n FDC reset, b0 terminal count, b1/b2 drive select)
//   85/H (mask 0xFF)  read        FDC status register
//   87/H (mask 0xFF)  read/write  FDC data register
//
// Media: Extended / Standard CPC DSK containers. Drive write back to the SD
// image is not implemented - mounted media behaves as write protected.
#pragma once

#include "InputStream.h"
#include <stdint.h>

class ZxSpectrumHcFdc {
public:
  static const int MAX_TRACKS = 164;   // 82 cylinders x 2 sides
  static const int MEDIA_MAX = 1024;   // largest sector payload kept in RAM
  static const int MFM_TRACK_BYTES = 6400; // ALEO raw MFM format: one encoded head track

  ZxSpectrumHcFdc() { reset(); }

  void reset();
  // Reset the controller state (machine reset); mounted media is kept.
  void softReset();
  void setPlus3Mode(bool enabled);
  void setPlus3Motor(bool enabled);
  // TIM-S Plus (fuse peripherals/disk/tims_fdc.c): uPD765A @ 4MHz, Shugart
  // drives A/B wired as FDC units 0/1 (units 2/3 never alias). Drive select
  // comes from the command unit bits, the motor is external - driven by the
  // $1FFD "secondary" latch bit 3 via setTimsMotor() (direct follow, no 555).
  void setTimsMode(bool enabled);
  void setTimsMotor(bool on);
  // byExtension: skip signature validation (files chosen via the +3 disk menu); structural checks still apply.
  bool mount(int drive, InputStream *is, bool byExtension = false);
  void eject(int drive);
  void frameTick(); // 50 Hz call: releases the 555 motor monostable tail

  uint8_t readLatch();
  void writeLatch(uint8_t value);
  void writeDataPc(uint16_t pc, uint8_t value) { _cmdPc = pc; writeData(value); }

  uint8_t readStatus();
  uint8_t readData();
  void writeData(uint8_t value);

  inline bool mediaPresent(int drive) const { return _loaded[drive]; }
  inline const char *mediaName() const { return _loaded[0] ? "A" : "-"; }

  // Debug trace ring (newest event at index 0), rendered by the on-screen
  // menu debug page. tag plus up to 8 nibbles-padded bytes.
  static const int TRACE_LINES = 24;
  static const int TRACE_LINE_MAX = 28;
  static void trace(char tag, const uint8_t *v, int n);
  static int traceLine(int idx, char *dst, int cap);
  inline uint8_t dbgLatch() const { return _latch; }
  inline bool dbgMotor() const { return _motorOn; }
  inline uint32_t dbgStatusPolls() const { return _statusPolls; }
  inline uint32_t dbgDataBytes() const { return _dataBytes; }

  // TIM FDC diag counters (volatile, cumulative across resets): live FDC
  // state for the disk menu TIMS PLUS rows. Strippable later - see the
  // 20261108 TASK_STATUS hook list.
  volatile uint32_t cmdBytes;   // bytes accepted in the command phase
  volatile uint32_t rdData;     // READ DATA commands issued
  volatile uint32_t rdId;       // READ ID commands issued
  volatile uint32_t seekCmds;   // SEEK / RECALIBRATE commands issued
  volatile uint32_t sdCmds;     // SENSE DRIVE STATUS commands issued
  volatile uint32_t noDataErr;  // results with ST1 no-data bit
  volatile uint32_t crcErr;     // results with data-field mark/CRC bits
  volatile uint32_t bytesRead;  // sector data bytes delivered to the CPU
  volatile uint8_t lastC, lastH, lastR, lastN; // CHRN of the last found sector
  volatile uint8_t lastSt0;     // ST0 of the last completed command
  volatile uint8_t lastSt3;     // result byte of the last SENSE DRIVE STATUS
  volatile uint32_t resBytes;   // result-phase bytes returned via readData()
  volatile uint8_t resVals[6];  // last 6 result-phase bytes returned
  volatile uint32_t motorWrites; // setTimsMotor() calls with true

private:
  enum Phase { PH_RESET, PH_COMMAND, PH_EXEC, PH_RESULT };

  struct Track {
    uint32_t base;
    uint8_t sectorCount;
    uint8_t sizeCode;   // standard DSK: track header byte 20
  };
  struct Sector {
    uint32_t imgOff;
    uint16_t len;
    uint8_t c, h, r, n;
    uint8_t st1, st2;
  };

  uint8_t opcode() const { return _cmd[0] & 0x1f; }
  bool readsToCpu() const { uint8_t op = opcode(); return op == 2 || op == 6 || op == 0x0a || op == 0x0c; }
  bool writesMedia() const { uint8_t op = opcode(); return op == 5 || op == 9; }
  bool scanning() const { uint8_t op = opcode(); return op == 0x11 || op == 0x19 || op == 0x1d; }

  bool loadTrackTable(int drive, uint8_t side, Track &tr);
  int findSector(int drive, bool sequential, Sector &out);
  bool startBufferedSector(const Sector &s);
  void execute();
  void beginTransfer();
  void startLoadedSector();
  void advanceByte(bool fromDataRead);
  void completeSector();
  void finish(uint8_t st1, uint8_t st2);
  void advanceId();
  void setSingleResult(uint8_t st0);
  void seekStart();
  bool driveUsable(int unit);

  InputStream *_is[2];
  bool _loaded[2];
  bool _extFmt[2];
  bool _mfmFmt[2];
  bool _plus3Mode;
  bool _timsMode;
  uint8_t _cylCount[2], _sideCount[2];
  uint32_t _trackIndex[2][MAX_TRACKS];
  uint8_t _pcn[2];

  Phase _phase;
  uint8_t _latch;
  bool _motorOn;
  uint16_t _motorCount;

  uint8_t _cmd[9];
  uint8_t _cmdPos, _cmdLen;
  uint8_t _result[8];
  uint8_t _resultCount, _resultPos;
  uint8_t _intStatus[8], _intCyl[8], _intCount;
  uint32_t _statusPolls;
  uint32_t _dataBytes;
  uint16_t _cmdPc;

  int _activeDrive;      // unit transferred to, or -1
  uint8_t _unit, _side;
  uint8_t _c, _h, _r, _n, _eot;
  uint8_t _st1, _st2;
  bool _terminal, _byteReady;
  uint8_t _trackCount;
  uint16_t _byteIndex, _xferLen;
  bool _scanEq, _scanSat;
  Track _tr;
  Sector _sector;
  uint8_t _media[MEDIA_MAX];
  uint8_t _trackBuf[248]; // 24 + 8 * 28-byte id table
  uint8_t _mfmTrackBuf[MFM_TRACK_BYTES];
  uint32_t _trackDataOffset[28];
  uint16_t _trackDataLength[28];
};
