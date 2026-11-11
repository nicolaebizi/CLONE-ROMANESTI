// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#include "CobraFloppy.h"
#include <string.h>

#ifdef COBRA_FDC_TRACE
#include <stdio.h>
#define TRACE(...) printf(__VA_ARGS__)
#else
#define TRACE(...)
#endif

// Timing, in Z80 T-states (CPU clock 3.5MHz).
//   MFM double density, 250 kbit/s: one byte every 32us.
#define T_PER_MS        3500
#define T_BYTE          112
#define T_SECTOR_FIND   (2 * T_PER_MS / 10)  // ID search before the first byte
#define T_NOT_FOUND     (4 * T_PER_MS)       // give up on a missing sector
#define T_ID_GAP        (8 * T_BYTE)         // between consecutive sectors

//----------------------------------------------------------------------------
// Disk image
//----------------------------------------------------------------------------

bool CobraDisk::_rawInterleaved = true;

CobraDisk::CobraDisk() :
  _media(0),
  _format(RAW),
  _wp(true),
  _cyls(0),
  _sides(0),
  _sideBytes(0),
  _spt(0),
  _n(0),
  _cacheTrack(-1),
  _cacheCount(0)
{
}

void CobraDisk::close() {
  _media = 0;
  _cacheTrack = -1;
}

bool CobraDisk::open(CobraDiskMedia *media) {
  close();
  if (!media) return false;
  uint8_t h[256];
  memset(h, 0, sizeof(h));
  const uint32_t size = media->size();
  if (size >= 256 && media->readAt(0, h, 256) == 256 &&
      (memcmp(h, "EXTENDED", 8) == 0 || memcmp(h, "MV - CPC", 8) == 0)) {
    const bool extended = h[0] == 'E';
    _format = extended ? EDSK : DSK;
    _cyls = h[0x30];
    _sides = h[0x31];
    if (_sides < 1 || _sides > 2 || _cyls == 0) return false;
    if ((uint32_t)_cyls * _sides > MAX_TRACKS) _cyls = MAX_TRACKS / _sides;
    const uint32_t tracks = (uint32_t)_cyls * _sides;
    const uint32_t fixed = (uint32_t)h[0x32] | ((uint32_t)h[0x33] << 8);
    uint32_t pos = 0x100;
    for (uint32_t t = 0; t < tracks; ++t) {
      const uint32_t ts = extended ? ((uint32_t)h[0x34 + t] << 8) : fixed;
      _trackPos[t] = (ts && pos + ts <= size) ? pos : 0;
      pos += ts;
    }
  }
  else {
    // Raw sector dump: cylinder by cylinder, side 0 then side 1, sectors 1..n.
    // The CoBra CP/M disk is 80 cylinders x 2 sides x 9 sectors x 512 bytes.
    _format = RAW;
    _n = 2;
    _sides = 2;
    _spt = 9;
    switch (size) {
      case 184320:  _cyls = 40; _sides = 1; break;
      case 368640:  _cyls = 40; break;
      case 409600:  _cyls = 40; _spt = 10; break;
      case 819200:  _cyls = 80; _spt = 10; break;
      default:
        if (size == 0 || size % 9216) return false;
        _cyls = size / 9216;
        break;
    }
    if (_cyls > MAX_TRACKS / 2) return false;
    _sideBytes = (uint32_t)_spt * 512;
  }
  _media = media;
  _wp = !media->writable();
  _cacheTrack = -1;
  return true;
}

void CobraDisk::sizeHint(uint8_t n) {
  if (_format != RAW || n == _n || n < 1 || n > 3) return;
  const uint32_t len = 128u << n;
  if (_sideBytes % len) return;
  _n = n;
  _spt = (uint8_t)(_sideBytes / len);
  _cacheTrack = -1;
}

uint8_t CobraDisk::track(uint8_t cyl, uint8_t side, const Sector **ids) {
  *ids = _cache;
  if (!_media || cyl >= _cyls || side >= _sides) return 0;
  const int16_t t = (int16_t)(cyl * _sides + side);
  if (t == _cacheTrack) return _cacheCount;
  _cacheTrack = t;
  _cacheCount = 0;
  if (_format == RAW) {
    // 256 byte sector tracks (DEVIL) are formatted with physical interleave 2:
    // 1, 10, 2, 11, ... 9, 18.  The 512 byte CP/M tracks are not interleaved.
    const uint16_t len = 128u << _n;
    const bool interleave = _n == 1;
    const uint8_t half = (_spt + 1) / 2;
    for (uint8_t i = 0; i < _spt; ++i) {
      Sector &s = _cache[i];
      const uint8_t r = interleave ? ((i & 1) ? half + i / 2 + 1 : i / 2 + 1) : i + 1;
      const uint8_t slot = (interleave && _rawInterleaved) ? i : r - 1;
      s.c = cyl; s.h = side; s.r = r; s.n = _n;
      s.st1 = 0; s.st2 = 0;
      s.len = len;
      s.pos = (uint32_t)t * _sideBytes + (uint32_t)slot * len;
    }
    _cacheCount = _spt;
    return _cacheCount;
  }
  const uint32_t p = _trackPos[t];
  if (!p) return 0;
  uint8_t h[256];
  if (_media->readAt(p, h, 256) != 256 || memcmp(h, "Track-Info", 10) != 0) return 0;
  uint8_t count = h[0x15];
  if (count > MAX_SECTORS) count = MAX_SECTORS;
  uint32_t data = p + 0x100;
  for (uint8_t i = 0; i < count; ++i) {
    const uint8_t *e = &h[0x18 + 8 * i];
    Sector &s = _cache[i];
    s.c = e[0]; s.h = e[1]; s.r = e[2]; s.n = e[3];
    s.st1 = e[4]; s.st2 = e[5];
    if (_format == EDSK) {
      s.len = (uint16_t)(e[6] | (e[7] << 8));
    }
    else {
      const uint8_t n = h[0x14] > 6 ? 6 : h[0x14];
      s.len = (uint16_t)(128u << n);
    }
    s.pos = data;
    data += s.len;
  }
  _cacheCount = count;
  return count;
}

bool CobraDisk::read(const Sector *s, uint8_t *buf, uint16_t len) {
  const uint16_t l = len < s->len ? len : s->len;
  const int32_t r = _media ? _media->readAt(s->pos, buf, l) : -1;
  const uint16_t got = r > 0 ? (uint16_t)r : 0;
  if (got < len) memset(buf + got, 0, len - got);
  return got == l;
}

bool CobraDisk::write(const Sector *s, const uint8_t *buf, uint16_t len) {
  if (!_media || _wp) return false;
  const uint16_t l = len < s->len ? len : s->len;
  return _media->writeAt(s->pos, buf, l) == (int32_t)l;
}

void CobraDisk::format(uint8_t cyl, uint8_t side, uint8_t filler) {
  const Sector *ids;
  const uint8_t count = track(cyl, side, &ids);
  uint8_t fill[128];
  memset(fill, filler, sizeof(fill));
  for (uint8_t i = 0; i < count; ++i) {
    for (uint16_t o = 0; o < ids[i].len; o += sizeof(fill)) {
      const uint16_t l = (uint32_t)(ids[i].len - o) < sizeof(fill) ? ids[i].len - o : sizeof(fill);
      _media->writeAt(ids[i].pos + o, fill, l);
    }
  }
}

//----------------------------------------------------------------------------
// Z80-CTC
//----------------------------------------------------------------------------

// Control word bits
#define CTC_INT       0x80  // interrupt enable
#define CTC_COUNTER   0x40  // 1 = counter mode, 0 = timer mode
#define CTC_P256      0x20  // timer prescaler 256 (else 16)
#define CTC_RISING    0x10  // CLK/TRG active edge
#define CTC_TRIGGER   0x08  // timer started by CLK/TRG
#define CTC_TC        0x04  // time constant follows
#define CTC_RESET     0x02  // software reset
#define CTC_CONTROL   0x01  // control word (else interrupt vector)

CobraCtc::CobraCtc() : _vector(0), _zc(0), _zcContext(0) {
  reset();
}

void CobraCtc::reset() {
  for (int i = 0; i < 4; ++i) {
    Channel &c = _ch[i];
    c.control = 0;
    c.tc = 0;
    c.count = 256;
    c.prescale = 0;
    c.tcPending = false;
    c.running = false;
    c.waitTrigger = false;
    c.level = false;
    c.intPending = false;
    c.inService = false;
  }
}

uint8_t CobraCtc::read(uint8_t ch) {
  return (uint8_t)_ch[ch & 3].count;
}

void CobraCtc::write(uint8_t ch, uint8_t value) {
  Channel &c = _ch[ch & 3];
  TRACE("CTC%d <- %02X%s\n", ch & 3, value, c.tcPending ? " (tc)" : "");
  if (c.tcPending) {
    c.tcPending = false;
    c.tc = value;
    if (!c.running && !c.waitTrigger) {
      // Stopped (reset) channel: load the counter and start.
      c.count = value ? value : 256;
      c.prescale = 0;
      if (!(c.control & CTC_COUNTER) && (c.control & CTC_TRIGGER)) c.waitTrigger = true;
      else c.running = true;
    }
    return;
  }
  if (value & CTC_CONTROL) {
    c.control = value;
    if (value & CTC_RESET) {
      c.running = false;
      c.waitTrigger = false;
    }
    if (!(value & CTC_INT)) c.intPending = false;
    c.tcPending = (value & CTC_TC) != 0;
  }
  else if ((ch & 3) == 0) {
    _vector = value & 0xF8;
  }
}

void CobraCtc::decrement(uint8_t ch) {
  Channel &c = _ch[ch];
  if (--c.count == 0) {
    c.count = c.tc ? c.tc : 256;
    if (c.control & CTC_INT) c.intPending = true;
    if (_zc) _zc(_zcContext, ch);
  }
}

void CobraCtc::edge(uint8_t ch, bool rising) {
  Channel &c = _ch[ch];
  if (((c.control & CTC_RISING) != 0) != rising) return;
  if (c.waitTrigger) {
    c.waitTrigger = false;
    c.running = true;
    c.prescale = 0;
    return;
  }
  if (c.running && (c.control & CTC_COUNTER)) decrement(ch);
}

void CobraCtc::trigger(uint8_t ch, bool level) {
  Channel &c = _ch[ch];
  if (c.level == level) return;
  c.level = level;
  edge(ch, level);
}

void CobraCtc::pulse(uint8_t ch) {
  trigger(ch, true);
  trigger(ch, false);
}

void CobraCtc::clock(uint32_t t) {
  for (uint8_t i = 0; i < 4; ++i) {
    Channel &c = _ch[i];
    if (!c.running || (c.control & CTC_COUNTER)) continue;
    const uint32_t p = (c.control & CTC_P256) ? 256 : 16;
    uint32_t s = c.prescale + t;
    while (s >= p) {
      s -= p;
      decrement(i);
    }
    c.prescale = (uint16_t)s;
  }
}

// Channel 0 has the highest priority.  A channel under service blocks
// itself and every lower priority channel until its RETI.
bool CobraCtc::intLine() const {
  for (int i = 0; i < 4; ++i) {
    if (_ch[i].inService) return false;
    if (_ch[i].intPending) return true;
  }
  return false;
}

uint8_t CobraCtc::acknowledge() {
  for (uint8_t i = 0; i < 4; ++i) {
    Channel &c = _ch[i];
    if (c.inService) break;
    if (c.intPending) {
      c.intPending = false;
      c.inService = true;
      TRACE("CTC%d INTA vector %02X\n", i, _vector | (i << 1));
      return (uint8_t)(_vector | (i << 1));
    }
  }
  return 0xFF;
}

void CobraCtc::reti() {
  for (int i = 0; i < 4; ++i) {
    if (_ch[i].inService) {
      _ch[i].inService = false;
      return;
    }
  }
}

//----------------------------------------------------------------------------
// 8272A
//----------------------------------------------------------------------------

// Main status register
#define MSR_RQM 0x80
#define MSR_DIO 0x40
#define MSR_EXM 0x20
#define MSR_CB  0x10

// Status register bits
#define ST0_ABNORMAL 0x40
#define ST0_INVALID  0x80
#define ST0_SE       0x20
#define ST0_NR       0x08
#define ST1_EN       0x80
#define ST1_DE       0x20
#define ST1_ND       0x04
#define ST1_NW       0x02
#define ST1_MA       0x01
#define ST2_CM       0x40
#define ST2_DD       0x20
#define ST2_WC       0x10
#define ST2_SN       0x04
#define ST2_BC       0x02
#define ST3_WP       0x40
#define ST3_RDY      0x20
#define ST3_T0       0x10

// Command byte bits
#define CMD_MT 0x80
#define CMD_SK 0x20

static uint8_t commandLength(uint8_t cmd) {
  switch (cmd & 0x1F) {
    case 0x02: return 9; // Read Track
    case 0x03: return 3; // Specify
    case 0x04: return 2; // Sense Drive Status
    case 0x05: return 9; // Write Data
    case 0x06: return 9; // Read Data
    case 0x07: return 2; // Recalibrate
    case 0x08: return 1; // Sense Interrupt Status
    case 0x09: return 9; // Write Deleted Data
    case 0x0A: return 2; // Read ID
    case 0x0C: return 9; // Read Deleted Data
    case 0x0D: return 6; // Format Track
    case 0x0F: return 3; // Seek
    case 0x11: return 9; // Scan Equal
    case 0x19: return 9; // Scan Low or Equal
    case 0x1D: return 9; // Scan High or Equal
    default:   return 0; // invalid
  }
}

CobraFdc::CobraFdc() {
  for (uint32_t i = 0; i < DRIVES; ++i) _disk[i] = 0;
  reset();
}

void CobraFdc::reset() {
  _phase = COMMAND;
  _cmdLen = 0;
  _cmdPos = 0;
  _resLen = 0;
  _resPos = 0;
  _int = false;
  _rqm = true;
  _data = 0xFF;
  for (uint32_t i = 0; i < DRIVES; ++i) {
    _pcn[i] = 0;
    _seekSt0[i] = 0;
    _idIndex[i] = 0;
    _seekTimer[i] = 0;
    _seekTarget[i] = 0;
  }
  _seekBusy = 0;
  _seekEnd = 0;
  _srt = 16;
  _event = EV_NONE;
  _timer = 0;
  _tc = false;
  _byteHeld = false;
  _sector = 0;
}

bool CobraFdc::ready(uint8_t unit) const {
  return _disk[unit] && _disk[unit]->mounted();
}

void CobraFdc::mediaChanged(uint8_t unit) {
  _idIndex[unit] = 0;
}

uint8_t CobraFdc::readStatus() {
  uint8_t msr = _seekBusy;
  switch (_phase) {
    case COMMAND:
      msr |= MSR_RQM;
      if (_cmdPos) msr |= MSR_CB;
      break;
    case EXEC_READ:
      msr |= MSR_CB | MSR_EXM | MSR_DIO;
      if (_byteHeld) msr |= MSR_RQM;
      break;
    case EXEC_WRITE:
    case EXEC_FORMAT:
      msr |= MSR_CB | MSR_EXM;
      if (_byteHeld) msr |= MSR_RQM;
      break;
    case RESULT:
      msr |= MSR_CB | MSR_RQM | MSR_DIO;
      break;
  }
  return msr;
}

void CobraFdc::result(uint8_t n, bool interrupt) {
  _resLen = n;
  _resPos = 0;
  _phase = RESULT;
  _event = EV_NONE;
  if (interrupt) _int = true;
}

uint8_t CobraFdc::readData() {
  switch (_phase) {
    case RESULT: {
      _int = false;
      _data = _res[_resPos++];
      if (_resPos >= _resLen) {
        _phase = COMMAND;
        _cmdPos = 0;
      }
      return _data;
    }
    case EXEC_READ: {
      if (!_byteHeld) return _data;
      const uint8_t v = _data;
      _byteHeld = false;
      _int = false;
      ++_pos;
      if (_tc) {
        endTransfer(false);
      }
      else if (_pos < _len) {
        scheduleByte(T_BYTE);
      }
      else if (_st1 & ST1_DE) {
        endTransfer(true);
      }
      else if ((_st2 & ST2_CM) || !nextSector()) {
        // A deleted data mark ends the command after its sector, and so does
        // the last sector of the cylinder (EN) when no TC has come.
        if (!(_st2 & ST2_CM)) _st1 |= ST1_EN;
        endTransfer(!(_st2 & ST2_CM));
      }
      return v;
    }
    default:
      return _data;
  }
}

void CobraFdc::writeData(uint8_t value) {
  switch (_phase) {
    case COMMAND: {
      if (_cmdPos == 0) {
        _cmd[0] = value;
        _cmdLen = commandLength(value);
        if (_cmdLen == 0) {
          _res[0] = ST0_INVALID;
          result(1, false);
          return;
        }
      }
      else {
        _cmd[_cmdPos] = value;
      }
      if (++_cmdPos >= _cmdLen) execute();
      return;
    }
    case EXEC_WRITE: {
      if (!_byteHeld) return;
      _byteHeld = false;
      _int = false;
      _buf[_pos++] = value;
      if (_tc && _pos < _len) {
        // TC mid-sector: the rest of the sector is written as zeros.
        memset(_buf + _pos, 0, _len - _pos);
        _pos = _len;
      }
      if (_pos < _len) {
        scheduleByte(T_BYTE);
        return;
      }
      if (!_scan && !_disk[_unit]->write(_sector, _buf, _len)) {
        _st1 |= ST1_NW;
        endTransfer(true);
        return;
      }
      if (_tc) {
        endTransfer(false);
      }
      else if (!nextSector()) {
        _st1 |= ST1_EN;
        endTransfer(true);
      }
      return;
    }
    case EXEC_FORMAT: {
      if (!_byteHeld) return;
      _byteHeld = false;
      _int = false;
      _fmtId[_pos++] = value;
      if (_pos < 4) {
        scheduleByte(T_BYTE);
        return;
      }
      _pos = 0;
      if (--_fmtCount && !_tc) {
        scheduleByte((int32_t)(128u << (_cmd[2] & 7)) * T_BYTE);
        return;
      }
      _disk[_unit]->format(_pcn[_unit], _head, _cmd[5]);
      _c = _fmtId[0]; _h = _fmtId[1]; _r = _fmtId[2]; _n = _fmtId[3];
      endTransfer(false);
      return;
    }
    default:
      return;
  }
}

void CobraFdc::scheduleByte(int32_t t) {
  _event = EV_BYTE;
  _timer = t;
}

// Locate sector _c/_h/_r/_n on the track under the selected head.
bool CobraFdc::findSector() {
  const CobraDisk::Sector *ids;
  const uint8_t count = _disk[_unit]->track(_pcn[_unit], _head, &ids);
  if (!count) {
    _st1 |= ST1_MA;
    return false;
  }
  for (uint8_t i = 0; i < count; ++i) {
    const CobraDisk::Sector &s = ids[i];
    if (s.r == _r && s.c == _c && s.h == _h && s.n == _n) {
      _sector = &s;
      return true;
    }
  }
  for (uint8_t i = 0; i < count; ++i) {
    if (ids[i].r == _r && ids[i].c != _c) {
      _st2 |= ids[i].c == 0xFF ? ST2_BC : ST2_WC;
      break;
    }
  }
  _st1 |= ST1_ND;
  return false;
}

bool CobraFdc::loadSector() {
  const uint8_t n = _n > 3 ? 3 : _n;
  _len = _n ? (uint16_t)(128u << n) : (uint16_t)(_cmd[8] ? _cmd[8] : 128);
  _pos = 0;
  const bool deleted = (_sector->st2 & ST2_CM) != 0;
  const bool wantDeleted = (_cmd[0] & 0x1F) == 0x0C;
  if (deleted != wantDeleted) _st2 |= ST2_CM;
  if (_phase == EXEC_READ) {
    _disk[_unit]->read(_sector, _buf, _len);
    if ((_sector->st1 & ST1_DE) || (_sector->st2 & ST2_DD)) {
      _st1 |= ST1_DE;
      _st2 |= ST2_DD;
    }
  }
  return true;
}

// Advance to the next sector of a multi-sector transfer.  Returns false at
// the end of the cylinder.
bool CobraFdc::nextSector() {
  const uint8_t eot = _cmd[6];
  if ((_cmd[0] & 0x1F) == 0x02) {
    // Read Track: sectors in physical order, EOT of them.
    if (++_trackSector >= eot) return false;
    const CobraDisk::Sector *ids;
    const uint8_t count = _disk[_unit]->track(_pcn[_unit], _head, &ids);
    if (_trackSector >= count) return false;
    _sector = &ids[_trackSector];
    _r = _sector->r;
    loadSector();
    scheduleByte(T_ID_GAP);
    return true;
  }
  if (_r != eot) {
    ++_r;
  }
  else if ((_cmd[0] & CMD_MT) && !_head) {
    _head = 1;
    _h ^= 1;
    _r = 1;
  }
  else {
    return false;
  }
  if (!findSector()) {
    endTransfer(true);
    return true;
  }
  loadSector();
  scheduleByte(T_ID_GAP);
  return true;
}

// Result phase of the read/write/format family.  _c/_h/_r/_n end up as the
// ID of the next sector after a completed one (8272A data sheet, table 3).
void CobraFdc::endTransfer(bool abnormal) {
  const uint8_t cmd = _cmd[0] & 0x1F;
  const bool rw = cmd != 0x0D && cmd != 0x0A;
  const bool completed = rw && _sector &&
    !(_st1 & (ST1_ND | ST1_MA | ST1_NW | ST1_DE)) && !(_st0 & ST0_NR);
  int32_t t = T_BYTE;
  if (completed) {
    if (_pos < _len) t = (int32_t)(_len - _pos) * T_BYTE;
    if (_r == _cmd[6]) {
      _r = 1;
      if ((_cmd[0] & CMD_MT) && !(_h & 1)) _h ^= 1;
      else { ++_c; if (_cmd[0] & CMD_MT) _h ^= 1; }
    }
    else {
      ++_r;
    }
  }
  if (abnormal) _st0 |= ST0_ABNORMAL;
  if (cmd == 0x11 || cmd == 0x19 || cmd == 0x1D) _st2 |= ST2_SN;
  _res[0] = _st0 | (_head << 2) | _unit;
  _res[1] = _st1;
  _res[2] = _st2;
  _res[3] = _c;
  _res[4] = _h;
  _res[5] = _r;
  _res[6] = _n;
  TRACE("FDC result %02X %02X %02X C%d H%d R%d N%d (pos %d/%d tc %d)\n", _res[0], _res[1], _res[2], _c, _h, _r, _n, _pos, _len, _tc);
  _byteHeld = false;
  _event = EV_RESULT;
  _timer = t;
}

void CobraFdc::terminalCount() {
  if (_phase != EXEC_READ && _phase != EXEC_WRITE && _phase != EXEC_FORMAT) return;
  if (_tc) return;
  _tc = true;
  // The byte being offered when TC arrives is still handed to the CPU; with
  // no byte pending the transfer ends right away.
  if (!_byteHeld && _event == EV_BYTE) {
    if (_phase == EXEC_WRITE) {
      if (_pos < _len) memset(_buf + _pos, 0, _len - _pos);
      if (!_scan) _disk[_unit]->write(_sector, _buf, _len);
      _pos = _len;
    }
    endTransfer(false);
  }
}

void CobraFdc::execute() {
  const uint8_t cmd = _cmd[0] & 0x1F;
  TRACE("FDC cmd %02X:", _cmd[0]);
  for (uint8_t i = 1; i < _cmdLen; ++i) TRACE(" %02X", _cmd[i]);
  TRACE("\n");
  _cmdPos = 0;
  _unit = _cmd[1] & 3;
  _head = (_cmd[1] >> 2) & 1;
  _st0 = 0;
  _st1 = 0;
  _st2 = 0;
  _tc = false;
  _scan = false;
  _byteHeld = false;
  _sector = 0;
  _pos = 0;
  _len = 0;
  switch (cmd) {
    case 0x03: // Specify
      _srt = 16 - (_cmd[1] >> 4);
      _phase = COMMAND;
      return;

    case 0x04: { // Sense Drive Status
      uint8_t st3 = (_head << 2) | _unit;
      if (ready(_unit)) {
        st3 |= ST3_RDY;
        if (_disk[_unit]->writeProtected()) st3 |= ST3_WP;
      }
      if (_pcn[_unit] == 0) st3 |= ST3_T0;
      _res[0] = st3;
      result(1, false);
      return;
    }

    case 0x07:   // Recalibrate
    case 0x0F: { // Seek
      const uint8_t target = cmd == 0x07 ? 0 : _cmd[2];
      const uint8_t from = _pcn[_unit];
      const uint32_t steps = target > from ? target - from : from - target;
      _seekTarget[_unit] = target;
      _seekBusy |= 1 << _unit;
      _seekSt0[_unit] = ST0_SE | (_head << 2) | _unit;
      if (!ready(_unit)) _seekSt0[_unit] |= ST0_ABNORMAL | ST0_NR;
      _seekTimer[_unit] = (int32_t)(steps ? steps : 1) * _srt * T_PER_MS;
      _phase = COMMAND;
      return;
    }

    case 0x08: { // Sense Interrupt Status
      for (uint8_t u = 0; u < DRIVES; ++u) {
        if (_seekEnd & (1 << u)) {
          _seekEnd &= ~(1 << u);
          _res[0] = _seekSt0[u];
          _res[1] = _pcn[u];
          if (!_seekEnd) _int = false;
          result(2, false);
          return;
        }
      }
      _res[0] = ST0_INVALID;
      result(1, false);
      return;
    }

    case 0x0A: { // Read ID
      _phase = EXEC_READ;
      _n = 0;
      if (!ready(_unit)) {
        _st0 |= ST0_NR;
        endTransfer(true);
        return;
      }
      const CobraDisk::Sector *ids;
      const uint8_t count = _disk[_unit]->track(_pcn[_unit], _head, &ids);
      if (!count) {
        _st1 |= ST1_MA;
        endTransfer(true);
        _timer = T_NOT_FOUND;
        return;
      }
      const CobraDisk::Sector &s = ids[_idIndex[_unit]++ % count];
      _c = s.c; _h = s.h; _r = s.r; _n = s.n;
      endTransfer(false);
      _timer = T_SECTOR_FIND;
      return;
    }

    case 0x0D: { // Format Track
      _phase = EXEC_FORMAT;
      _c = _pcn[_unit]; _h = _head; _r = 1; _n = _cmd[2];
      if (!ready(_unit)) {
        _st0 |= ST0_NR;
        endTransfer(true);
        return;
      }
      if (_disk[_unit]->writeProtected()) {
        _st1 |= ST1_NW;
        endTransfer(true);
        return;
      }
      _disk[_unit]->sizeHint(_cmd[2]);
      _fmtCount = _cmd[3] ? _cmd[3] : 1;
      scheduleByte(T_SECTOR_FIND);
      return;
    }

    case 0x02:   // Read Track
    case 0x06:   // Read Data
    case 0x0C:   // Read Deleted Data
    case 0x05:   // Write Data
    case 0x09:   // Write Deleted Data
    case 0x11:   // Scan Equal
    case 0x19:   // Scan Low or Equal
    case 0x1D: { // Scan High or Equal
      const bool write = cmd == 0x05 || cmd == 0x09;
      _scan = cmd == 0x11 || cmd == 0x19 || cmd == 0x1D;
      _phase = (write || _scan) ? EXEC_WRITE : EXEC_READ;
      _c = _cmd[2]; _h = _cmd[3]; _r = _cmd[4]; _n = _cmd[5];
      if (!ready(_unit)) {
        _st0 |= ST0_NR;
        endTransfer(true);
        return;
      }
      if (write && _disk[_unit]->writeProtected()) {
        _st1 |= ST1_NW;
        endTransfer(true);
        return;
      }
      if (cmd == 0x02) {
        _disk[_unit]->sizeHint(_n);
        const CobraDisk::Sector *ids;
        const uint8_t count = _disk[_unit]->track(_pcn[_unit], _head, &ids);
        _trackSector = 0;
        if (!count) {
          _st1 |= ST1_MA;
          endTransfer(true);
          _timer = T_NOT_FOUND;
          return;
        }
        _sector = &ids[0];
        if (_sector->r != _r || _sector->c != _c) _st1 |= ST1_ND;
        _r = _sector->r;
      }
      else {
        _disk[_unit]->sizeHint(_n);
        if (!findSector()) {
          endTransfer(true);
          _timer = T_NOT_FOUND;
          return;
        }
      }
      loadSector();
      scheduleByte(T_SECTOR_FIND);
      return;
    }

    default:
      _res[0] = ST0_INVALID;
      result(1, false);
      return;
  }
}

void CobraFdc::clock(uint32_t t) {
  if (_seekBusy) {
    for (uint8_t u = 0; u < DRIVES; ++u) {
      if (!(_seekBusy & (1 << u))) continue;
      _seekTimer[u] -= (int32_t)t;
      if (_seekTimer[u] > 0) continue;
      _seekBusy &= ~(1 << u);
      _pcn[u] = _seekTarget[u];
      _seekEnd |= 1 << u;
      _int = true;
    }
  }
  if (_event == EV_NONE) return;
  _timer -= (int32_t)t;
  if (_timer > 0) return;
  const Event e = _event;
  _event = EV_NONE;
  if (e == EV_BYTE) {
    if (_phase == EXEC_READ) _data = _buf[_pos];
    _byteHeld = true;
    _int = true;
  }
  else if (e == EV_RESULT) {
    result(7, true);
  }
}

//----------------------------------------------------------------------------
// Interface board
//----------------------------------------------------------------------------

CobraFloppy::CobraFloppy() : _fdcInt(false) {
  _ctc.setZcHandler(zc, this);
  for (uint8_t i = 0; i < CobraFdc::DRIVES; ++i) _fdc.attach(i, &_disk[i]);
}

void CobraFloppy::reset() {
  _ctc.reset();
  _fdc.reset();
  _fdcInt = false;
}

void CobraFloppy::cpuReset() {
  _ctc.reset();
  _fdcInt = false;
  syncInt();
}

void CobraFloppy::zc(void *context, uint8_t channel) {
  CobraFloppy *self = (CobraFloppy *)context;
  switch (channel) {
    case 0: self->_ctc.pulse(1); break;
    case 1: self->_ctc.pulse(2); break;
    case 2: self->_fdc.terminalCount(); break;
    default: break;
  }
}

void CobraFloppy::syncInt() {
  const bool i = _fdc.intLine();
  if (i != _fdcInt) {
    _fdcInt = i;
    _ctc.trigger(0, i);
  }
}

uint8_t CobraFloppy::ioRead(uint16_t address) {
  uint8_t v = 0xFF;
  if (ctcPort(address)) v &= _ctc.read((address >> 3) & 3);
  if (fdcPort(address)) v &= (address & 0x08) ? _fdc.readData() : _fdc.readStatus();
  syncInt();
  return v;
}

void CobraFloppy::ioWrite(uint16_t address, uint8_t value) {
  if (ctcPort(address)) _ctc.write((address >> 3) & 3, value);
  if (fdcPort(address) && (address & 0x08)) _fdc.writeData(value);
  syncInt();
}

void CobraFloppy::clock(uint32_t t) {
  _fdc.clock(t);
  syncInt();
  _ctc.clock(t);
}

void CobraFloppy::frame() {
  _ctc.pulse(3);
}

bool CobraFloppy::insert(uint8_t unit, CobraDiskMedia *media) {
  if (unit >= CobraFdc::DRIVES) return false;
  _disk[unit].close();
  const bool ok = media ? _disk[unit].open(media) : false;
  _fdc.mediaChanged(unit);
  return ok;
}
