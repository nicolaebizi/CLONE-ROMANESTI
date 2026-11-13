// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#pragma once

#include <stdint.h>

// CoBra 1 floppy disk interface v.1.2 (ElectroNNix schematic rev. 2.42).
//
//   U01 Z80-CTC   /CE = BA2, CS0 = BA3, CS1 = BA4
//                 -> channel 0..3 at $E3, $EB, $F3, $FB (any port with A2=0)
//   U02 8272A     /CS = BA1, A0 = BA3
//                 -> main status register $F5, data register $FD (A1=0)
//
// Signal wiring between the two chips (sheet 3):
//   8272 INT  -> CTC CLK/TRG0      ZC/TO0 -> CLK/TRG1     ZC/TO1 -> CLK/TRG2
//   ZC/TO2    -> 8272 TC           CLK/TRG3 <- JEXA/9 (TRG3 = NVS frame pulse, JP21)
//   8272 DRQ is pulled up and DACK unused: the controller always runs in
//   non-DMA mode, signalling every data byte on INT.  The CTC turns these
//   INT edges into IM2 interrupts and counts the bytes of a transfer, ending
//   it with TC once the programmed count has elapsed.
//   US0/US1 select one of four drives (74LS155), so FDC unit n = drive n.

// Random access storage behind a mounted disk image.
class CobraDiskMedia {
public:
  virtual ~CobraDiskMedia() {}
  // Both return the number of bytes transferred, or a negative error.
  virtual int32_t readAt(uint32_t pos, uint8_t *buf, uint32_t len) = 0;
  virtual int32_t writeAt(uint32_t pos, const uint8_t *buf, uint32_t len) = 0;
  virtual uint32_t size() = 0;
  virtual bool writable() = 0;
};

// A disk image: raw sector dump (IMG) or CPCEMU DSK / EXTENDED DSK.
class CobraDisk {
public:
  static const uint32_t MAX_TRACKS = 168;  // 84 cylinders x 2 sides
  static const uint32_t MAX_SECTORS = 29;  // per track (DSK track header limit)

  struct Sector {
    uint8_t c, h, r, n;  // ID field
    uint8_t st1, st2;    // EDSK recorded error flags
    uint16_t len;        // stored data length
    uint32_t pos;        // data offset in the image
  };

  CobraDisk();
  // Recognises the image format; false if the image is not usable.
  bool open(CobraDiskMedia *media);
  void close();
  inline bool mounted() const { return _media != 0; }
  inline bool writeProtected() const { return _wp; }
  // A raw image carries no format information.  Its 4608 byte track side is
  // cut into the sector size the operating system asks for: 9 x 512 (CP/M)
  // or 18 x 256 (DEVIL).  Called with the N of every Read/Write command.
  void sizeHint(uint8_t n);
  // Raw 18 x 256 images: true when the file stores the sectors in their
  // physical (interleaved 1,10,2,11,...) order, false for sector number order.
  static void rawInterleaved(bool interleaved) { _rawInterleaved = interleaved; }
  static bool rawInterleaved() { return _rawInterleaved; }
  // Sector IDs of physical track (cyl, side) in rotational order.
  // Returns the sector count; 0 = unformatted track.
  uint8_t track(uint8_t cyl, uint8_t side, const Sector **ids);
  bool read(const Sector *s, uint8_t *buf, uint16_t len);
  bool write(const Sector *s, const uint8_t *buf, uint16_t len);
  // Format Track: fill the sectors of a track with the filler byte.
  void format(uint8_t cyl, uint8_t side, uint8_t filler);

private:
  enum Format { RAW, DSK, EDSK };
  CobraDiskMedia *_media;
  Format _format;
  bool _wp;
  uint8_t _cyls;
  uint8_t _sides;
  // RAW geometry
  uint32_t _sideBytes;
  uint8_t _spt;
  uint8_t _n;
  // Raw 735232-byte site image: tracks 0-1 hold the protected CoBra-DOS
  // system sectors (one 4096-byte sector per side), tracks 2-79 the usual
  // 9 x 512 CP/M sectors.
  bool _sys;
  static bool _rawInterleaved;
  // DSK / EDSK: offset of each track info block (0 = unformatted)
  uint32_t _trackPos[MAX_TRACKS];
  // Decoded track cache
  int16_t _cacheTrack;
  uint8_t _cacheCount;
  Sector _cache[MAX_SECTORS];
};

// Z80-CTC (U01).
class CobraCtc {
public:
  // Called when a channel's down counter reaches zero (ZC/TO pulse).
  typedef void (*ZcHandler)(void *context, uint8_t channel);

  CobraCtc();
  void setZcHandler(ZcHandler h, void *context) { _zc = h; _zcContext = context; }
  void reset();
  uint8_t read(uint8_t ch);
  void write(uint8_t ch, uint8_t value);
  // CLK/TRG input level of a channel (edges are detected here).
  void trigger(uint8_t ch, bool level);
  // A ZC/TO style positive pulse on CLK/TRG (one rising and one falling edge).
  void pulse(uint8_t ch);
  // System clock (phi) elapsed, in T-states.
  void clock(uint32_t t);
  // Daisy chain: /INT, interrupt acknowledge and RETI decoding.
  bool intLine() const;
  uint8_t acknowledge();
  void reti();

private:
  struct Channel {
    uint8_t control;
    uint8_t tc;          // time constant
    uint16_t count;      // down counter (1..256)
    uint16_t prescale;   // timer prescaler counter
    bool tcPending;      // next write is a time constant
    bool running;        // counting
    bool waitTrigger;    // timer waiting for its CLK/TRG start edge
    bool level;          // last CLK/TRG level
    bool intPending;
    bool inService;
  };
  Channel _ch[4];
  uint8_t _vector;
  ZcHandler _zc;
  void *_zcContext;
  void edge(uint8_t ch, bool rising);
  void decrement(uint8_t ch);
};

// 8272A floppy disk controller (U02) in non-DMA mode.
class CobraFdc {
public:
  static const uint32_t DRIVES = 4;

  CobraFdc();
  void reset();
  void attach(uint8_t unit, CobraDisk *disk) { _disk[unit] = disk; }
  uint8_t readStatus();
  uint8_t readData();
  void writeData(uint8_t value);
  // Terminal count input (CTC ZC/TO2).
  void terminalCount();
  void clock(uint32_t t);
  inline bool intLine() const { return _int; }
  // Execution phase of a read/write/format command (DMA request window).
  inline bool executing() const {
    return _phase == EXEC_READ || _phase == EXEC_WRITE || _phase == EXEC_FORMAT;
  }
  // Media changed: a drive's head position survives, its state does not.
  void mediaChanged(uint8_t unit);

private:
  enum Phase { COMMAND, EXEC_READ, EXEC_WRITE, EXEC_FORMAT, RESULT };
  enum Event { EV_NONE, EV_BYTE, EV_RESULT };

  CobraDisk *_disk[DRIVES];
  Phase _phase;
  uint8_t _cmd[9];
  uint8_t _cmdLen;
  uint8_t _cmdPos;
  uint8_t _res[7];
  uint8_t _resLen;
  uint8_t _resPos;
  bool _int;
  bool _rqm;
  uint8_t _data;          // data register latch
  uint8_t _pcn[DRIVES];   // present cylinder number per drive
  uint8_t _seekBusy;      // D0B..D3B of the MSR
  uint8_t _seekEnd;       // drives with a pending seek end interrupt
  uint8_t _seekSt0[DRIVES];
  uint8_t _seekTarget[DRIVES];
  int32_t _seekTimer[DRIVES];
  uint8_t _idIndex[DRIVES];  // rotational position for Read ID
  uint8_t _srt;           // step rate (ms)
  // Timed event of the read/write/format family
  Event _event;
  int32_t _timer;
  // Data transfer
  uint8_t _unit, _head;
  uint8_t _c, _h, _r, _n;   // CHRN of the sector being transferred
  uint8_t _st0, _st1, _st2;
  bool _tc;                 // terminal count received
  bool _scan;               // Scan command: CPU bytes are compared, not written
  bool _byteHeld;           // byte ready / required, waiting for the CPU
  uint8_t _trackSector;     // Read Track: physical sector index
  uint16_t _pos;            // byte index in the sector
  uint16_t _len;            // bytes in the sector
  uint8_t _fmtCount;        // Format Track: sectors left
  uint8_t _fmtId[4];
  const CobraDisk::Sector *_sector;
  // Largest sector transferred in non-DMA mode: N=5 (4096 bytes) on the
  // protected CoBra-DOS system tracks.
  uint8_t _buf[4096];

  void execute();
  void result(uint8_t n, bool interrupt);
  bool findSector();
  bool loadSector();
  bool nextSector();
  void scheduleByte(int32_t t);
  void endTransfer(bool abnormal);
  bool ready(uint8_t unit) const;
};

// The whole interface board as seen on the CoBra I/O bus.
class CobraFloppy {
public:
  CobraFloppy();
  // NPOR: the whole board.
  void reset();
  // NRST (K1 "RESET BASIC"): only the CTC; the 8272 RST pin is on POR.
  void cpuReset();
  static inline bool ctcPort(uint16_t address) { return !(address & 0x04); }
  static inline bool fdcPort(uint16_t address) { return !(address & 0x02); }
  uint8_t ioRead(uint16_t address);
  void ioWrite(uint16_t address, uint8_t value);
  void clock(uint32_t t);
  void frame();
  inline bool intLine() const { return _ctc.intLine(); }
  inline uint8_t acknowledge() { return _ctc.acknowledge(); }
  inline void reti() { _ctc.reti(); }
  // Image mounted in drive 0..3 (A:..D:); media 0 ejects the disk.
  bool insert(uint8_t unit, CobraDiskMedia *media);
  void eject(uint8_t unit) { insert(unit, 0); }
  bool inserted(uint8_t unit) const { return _disk[unit].mounted(); }

private:
  CobraCtc _ctc;
  CobraFdc _fdc;
  CobraDisk _disk[CobraFdc::DRIVES];
  bool _fdcInt;
  static void zc(void *context, uint8_t channel);
  void syncInt();
};
