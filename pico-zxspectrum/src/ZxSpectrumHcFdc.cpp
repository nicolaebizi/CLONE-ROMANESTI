// SPDX-License-Identifier: MIT
// ICE Felix HC2000 i8272 FDC, instant polling model per the reference emulator.
#include "ZxSpectrumHcFdc.h"
#include <string.h>

static const char EDSK_MAGIC[] = "EXTENDED CPC DSK File\r\nDisk-Info\r\n";
static const char STD_MAGIC[] = "MV - CPCEMU Disk-File\r\nDisk-Info\r\n";
static const char TRACK_MAGIC[] = "Track-Info\r\n";
static const char MFM_MAGIC[] = "MFM_DISK";

static char traceBuf[24][32];
static uint8_t traceCount;

// fix2 diag ring 3: last 8 MSR ($2FFD main status register) values read with
// TIM mode active, newest-first overwrite index (same pattern as the $3FFD
// data-port read ring in ZxSpectrum.h). Declared without "static" so the
// disk menu (ZxSpectrumMenu.cpp) can link to them via extern declarations.
volatile uint8_t timsMsrVals[8];
volatile uint8_t timsMsrIdx = 0;

static void tracePutNib(unsigned v, char *&d) {
  static const char h[] = "0123456789ABCDEF";
  *d++ = h[(v >> 4) & 15];
  *d++ = h[v & 15];
}

void ZxSpectrumHcFdc::trace(char tag, const uint8_t *v, int n) {
  char *d = traceBuf[traceCount % TRACE_LINES];
  *d++ = tag;
  *d++ = ' ';
  for (int i = 0; i < n; i++) {
    if (i) *d++ = ' ';
    tracePutNib(v[i], d);
  }
  *d = 0;
  traceCount++;
}

int ZxSpectrumHcFdc::traceLine(int idx, char *dst, int cap) {
  int start = (int)traceCount - TRACE_LINES; if (start < 0) start = 0;
  int i = (int)traceCount - 1 - idx;
  if (i < start) { dst[0] = 0; return 0; }
  const char *src = traceBuf[i % TRACE_LINES];
  int len = 0;
  while (src[len] && len < cap - 1) { dst[len] = src[len]; len++; }
  dst[len] = 0;
  return len;
}

void ZxSpectrumHcFdc::reset() {
  static const uint8_t t = 0;
  trace('S', &t, 1);
  cmdBytes = rdData = rdId = seekCmds = sdCmds = noDataErr = crcErr = bytesRead = 0;
  resBytes = motorWrites = 0;
  for (int i = 0; i < 6; i++) resVals[i] = 0;
  lastC = lastH = lastR = lastN = lastSt0 = lastSt3 = 0;
  for (int i = 0; i < 2; i++) {
    _loaded[i] = false;
    _extFmt[i] = false;
    _mfmFmt[i] = false;
    _cylCount[i] = _sideCount[i] = 0;
    _pcn[i] = 0;
    for (int t = 0; t < MAX_TRACKS; t++) _trackIndex[i][t] = 0xFFFFFFFFu;
    _is[i] = 0;
  }
  _phase = PH_RESET;
  _latch = 0;
  _motorOn = false;
  _motorCount = 0;
  _cmdPos = _cmdLen = 0;
  _resultCount = _resultPos = 0;
  _intCount = 0;
  _activeDrive = -1;
  _plus3Mode = false;
  _timsMode = false;
  _terminal = _byteReady = false;
  _byteIndex = _xferLen = 0;
}

bool ZxSpectrumHcFdc::mount(int drive, InputStream *is, bool byExtension) {
  if (drive < 0 || drive > 1 || !is) return false;
  eject(drive);
  uint8_t hdr[256];
  if (is->seek(0) < 0) return false;
  if (is->read(hdr, 256) != 256) return false;

  bool extended = true, standard = true;
  for (uint8_t i = 0; i < 34; i++) if (hdr[i] != EDSK_MAGIC[i]) { extended = false; break; }
  for (uint8_t i = 0; i < 34; i++) if (hdr[i] != STD_MAGIC[i]) { standard = false; break; }
  if (!extended && !standard) {
    if (!byExtension) {
    // ALEO BASIC disks are MFM_DISK v2 images: 80 cylinders, two surfaces,
    // stored side-major as 160 consecutive 6400-byte track streams.
    if (memcmp(hdr, MFM_MAGIC, sizeof(MFM_MAGIC) - 1) != 0 || hdr[8] != 2) return false;
    const uint8_t cylinders = hdr[12];
    const uint8_t surfaces = (uint8_t)(hdr[16] + 1);
    const uint16_t trackCount = (uint16_t)cylinders * surfaces;
    if (cylinders < 1 || cylinders > 82 || surfaces != 2 || trackCount > MAX_TRACKS) return false;
    const uint32_t expectedSize = 256 + (uint32_t)trackCount * MFM_TRACK_BYTES;
    if (is->seek(expectedSize - 1) < 0 || is->readByte() < 0 || is->seek(0) < 0) return false;
    for (uint8_t c = 0; c < cylinders; c++)
      for (uint8_t h = 0; h < surfaces; h++)
        _trackIndex[drive][(uint16_t)c * surfaces + h] =
          256 + ((uint32_t)h * cylinders + c) * MFM_TRACK_BYTES;
    _cylCount[drive] = cylinders;
    _sideCount[drive] = surfaces;
    _extFmt[drive] = false;
    _mfmFmt[drive] = true;
    _is[drive] = is;
    _loaded[drive] = true;
    return true;
    }
    // Extension-only acceptance (+3 menu): infer container from the first 8
    // bytes — "EXTENDED" => EDSK, anything else => standard DSK (covers the
    // DU54 variant "MV - CPC format Disk Image (DU54)"). Structural checks
    // below (cylinders, sides, track offsets) still reject corrupt images.
    extended = memcmp(hdr, "EXTENDED", 8) == 0;
    standard = !extended;
  }

  uint8_t cyl = hdr[48], sides = hdr[49];
  uint16_t fixedLen = standard ? (uint16_t)(hdr[50] | (hdr[51] << 8)) : 0;
  if (cyl < 1 || cyl > 82 || sides < 1 || sides > 2) return false;
  if (standard && (fixedLen < 256 || (fixedLen & 0xFF) != 0)) return false;

  uint32_t base = 256;
  for (int i = 0; i < cyl * sides; i++) {
    if (extended) {
      uint16_t len = (uint16_t)hdr[52 + i] << 8;
      if (len) {
        if (len < 256) { eject(drive); return false; }
        _trackIndex[drive][i] = base;
        base += len;
      }
    } else {
      _trackIndex[drive][i] = base;
      base += fixedLen;
    }
  }
  _cylCount[drive] = cyl;
  _sideCount[drive] = sides;
  _extFmt[drive] = extended;
  _mfmFmt[drive] = false;
  _is[drive] = is;
  _loaded[drive] = true;
  return true;
}

void ZxSpectrumHcFdc::eject(int drive) {
  if (drive < 0 || drive > 1) return;
  if (_activeDrive == drive) { _activeDrive = -1; _phase = PH_COMMAND; _resultCount = _resultPos = 0; }
  _loaded[drive] = false;
  _mfmFmt[drive] = false;
  _is[drive] = 0;
  _cylCount[drive] = _sideCount[drive] = 0;
  for (int t = 0; t < MAX_TRACKS; t++) _trackIndex[drive][t] = 0xFFFFFFFFu;
}

void ZxSpectrumHcFdc::frameTick() {
  // TIM mode: motor follows the $1FFD signal directly (see setTimsMotor).
  if (_timsMode) return;
  // 555 monostable: motor stays armed about 1.7 s after the BIOS releases the
  // b3 trigger (85 frames at 50 Hz). Holding b3 high keeps the cap discharged.
  if (_motorOn && !(_latch & 8)) {
    if (_motorCount > 0 && --_motorCount == 0) { _motorOn = false; static const uint8_t p = 0; trace('M', &p, 1); }
  }
}

void ZxSpectrumHcFdc::softReset() {
  static const uint8_t t = 0;
  trace('S', &t, 1);
  _phase = PH_RESET;
  _latch = 0;
  _motorOn = false;
  _motorCount = 0;
  _cmdPos = _cmdLen = 0;
  _resultCount = _resultPos = 0;
  _intCount = 0;
  _activeDrive = -1;
  _terminal = _byteReady = false;
  _byteIndex = _xferLen = 0;
}

void ZxSpectrumHcFdc::setPlus3Mode(bool enabled) {
  _plus3Mode = enabled;
  if (!enabled) return;
  _latch = 0;
  _motorOn = false;
  _motorCount = 0;
  _phase = PH_COMMAND;
  _cmdPos = _cmdLen = 0;
  _resultCount = _resultPos = 0;
  _intCount = 0;
  _activeDrive = -1;
  _terminal = _byteReady = false;
  _byteIndex = _xferLen = 0;
}

void ZxSpectrumHcFdc::setPlus3Motor(bool enabled) {
  if (!_plus3Mode) return;
  if (enabled) {
    _latch |= 8;
    _motorOn = true;
    _motorCount = 85;
  } else {
    _latch &= (uint8_t)~8;
    _motorCount = 85;
  }
}

void ZxSpectrumHcFdc::setTimsMode(bool enabled) {
  if (!enabled) { _timsMode = false; return; } // media kept; state comes from softReset()
  _timsMode = true;
  _latch = 0;
  _motorOn = false;
  _motorCount = 0;
  _phase = PH_COMMAND;
  _cmdPos = _cmdLen = 0;
  _resultCount = _resultPos = 0;
  _intCount = 0;
  _activeDrive = -1;
  _terminal = _byteReady = false;
  _byteIndex = _xferLen = 0;
}

void ZxSpectrumHcFdc::setTimsMotor(bool on) {
  // Fuse tims_fdc_motor(): fdd_motoron(drives[i], !!(secondary & 8)) for both
  // drives - the motor line follows the $1FFD bit 3 signal directly, there is
  // no daemon/555 monostable in TIM mode.
  if (!_timsMode) return;
  if (on) motorWrites++; // diag: motor-on requests via $1FFD bit 3
  _motorOn = on;
  _motorCount = 85;
}

// -------------------------------------------------------------------- ports

uint8_t ZxSpectrumHcFdc::readLatch() {
  bool busy = _motorOn && (_latch & 6) != 0;
  return (uint8_t)(0xfc | (busy ? 0 : 1));
}

void ZxSpectrumHcFdc::writeLatch(uint8_t value) {
  uint8_t prev = _latch;
  trace('L', &value, 1);
  _latch = value & 0x1f;
  if (_latch & 8) { _motorOn = true; _motorCount = 85; }
  if ((_latch & 0x10) == 0) {
    if (_phase != PH_RESET) {
      _phase = PH_RESET;
      _cmdPos = _cmdLen = 0;
      _resultCount = _resultPos = 0;
      _intCount = 0;
      _pcn[0] = _pcn[1] = 0;
      _activeDrive = -1;
      _terminal = _byteReady = false;
    }
  } else if ((prev & 0x10) == 0) {
    if (_phase == PH_RESET) {
      _phase = PH_COMMAND;
      for (uint8_t i = 0; i < 4; i++) {
        _intStatus[i] = (uint8_t)(0xc0 | i);
        _intCyl[i] = 0;
      }
      _intCount = 4;
    }
  }
  if ((_latch & 1) && !(prev & 1)) {
    static const uint8_t tc = 1;
    trace('T', &tc, 1);
    if (_phase == PH_EXEC && opcode() != 0x0a) _terminal = true;
  }
}

uint8_t ZxSpectrumHcFdc::readStatus() {
  _statusPolls++;
  uint8_t v;
  if (_phase == PH_RESET) {
    // fix2: TIM idle must present RQM (uPD765 idle = 0x80, no DIO/CB). The
    // TIM board never drives the HC $FD latch reset line (writeLatch is
    // HC2000-only), so a TIM controller parked in the reset phase is still
    // an idle, command-ready uPD765. HC2000/+3 keep the legacy 0x00.
    v = _timsMode ? 0x80 : 0;
  } else {
    if (_phase == PH_EXEC && !_byteReady && _activeDrive >= 0) advanceByte(false);
    if (_phase == PH_COMMAND) v = 0x80 | (_cmdPos == 0 ? 0 : 0x10);
    // Result phase: RQM, DIO and CB.  EXM (bit 5) is only set during the
    // non-DMA execution phase; the TIM-S boot ROM reads data bytes while EXM
    // is set ($02AA loop) and must see it drop before the result bytes.
    else if (_phase == PH_RESULT) v = 0xd0;
    else v = 0x30 | (_byteReady ? 0x80 : 0) | (readsToCpu() ? 0x40 : 0);
  }
  // fix2 diag ring 3: every MSR read while TIM mode is active (the port
  // $2FFD path is the only TIM-mode readStatus caller).
  if (_timsMode) {
    timsMsrVals[(uint8_t)(timsMsrIdx) & 7] = v;
    ++timsMsrIdx;
  }
  return v;
}

uint8_t ZxSpectrumHcFdc::readData() {
  if (_phase == PH_RESULT) {
    uint8_t v = _result[_resultPos++];
    _dataBytes++;
    resBytes++; // diag: result-phase byte returned to the CPU
    for (int i = 5; i > 0; i--) resVals[i] = resVals[i - 1];
    resVals[0] = v;
    if (_resultPos >= _resultCount) { _resultCount = _resultPos = 0; _phase = PH_COMMAND; }
    return v;
  }
  if (_phase == PH_EXEC && !_byteReady && _activeDrive >= 0) advanceByte(true);
  if (_phase != PH_EXEC || !_byteReady || !readsToCpu() || _activeDrive < 0) {
    static uint8_t lastYPhase = 0xFF;
    if (lastYPhase != (uint8_t)_phase) { lastYPhase = (uint8_t)_phase; trace('Y', &lastYPhase, 1); }
    return 0xFF;
  }
  uint8_t data = _byteIndex < _xferLen ? _media[_byteIndex] : 0;
  _byteIndex++;
  _byteReady = false;
  _dataBytes++;
  bytesRead++; // diag: sector data actually delivered to the CPU
  return data;
}

// Reference Fdc8272.CommandLength: fixed command sizes for this BIOS.
static uint8_t commandLength(uint8_t op) {
  switch (op) {
    case 3: case 0x0f: return 3;
    case 4: case 7: case 0x0a: return 2;
    case 0x0d: return 6;
    case 1: case 2: case 5: case 6: case 9: case 0x0c: case 0x11: case 0x19: case 0x1d: return 9;
    default: return 1;
  }
}

void ZxSpectrumHcFdc::writeData(uint8_t value) {
  if (_phase == PH_COMMAND) {
    if (_cmdPos == 0) {
      _cmdLen = commandLength(value & 0x1f);
      uint8_t op = value & 0x1f; // diag dispatch counts (TIM menu rows)
      if (op == 6) rdData++;
      else if (op == 0x0a) rdId++;
      else if (op == 7 || op == 0x0f) seekCmds++;
    }
    _cmd[_cmdPos++] = value;
    cmdBytes++;
    if (_cmdPos >= _cmdLen) execute();
    return;
  }
  if (_phase == PH_EXEC && _byteReady && !readsToCpu()) {
    // Write commands: mounted image behaves write-protected (no write back).
    finish(2, 0);
    return;
  }
  uint8_t ph = (uint8_t)_phase;
  trace('W', &ph, 1);
}

// ---------------------------------------------------------------- internals

bool ZxSpectrumHcFdc::loadTrackTable(int drive, uint8_t side, Track &tr) {
  uint8_t cyl = _pcn[drive];
  if (!mediaPresent(drive) || cyl >= _cylCount[drive] || side >= _sideCount[drive]) return false;
  uint32_t ti = _trackIndex[drive][(uint16_t)cyl * _sideCount[drive] + side];
  if (ti == 0xFFFFFFFFu) return false;
  tr.base = ti;
  tr.sectorCount = 0;
  tr.sizeCode = 0;
  if (_mfmFmt[drive]) {
    if ((_is[drive])->seek(ti) < 0 || (_is[drive])->read(_mfmTrackBuf, MFM_TRACK_BYTES) != MFM_TRACK_BYTES)
      return false;
    uint32_t pos = 0;
    while (pos + 10 < MFM_TRACK_BYTES && tr.sectorCount < 28) {
      uint32_t idPos = pos;
      while (idPos + 10 <= MFM_TRACK_BYTES &&
             !(_mfmTrackBuf[idPos] == 0xA1 && _mfmTrackBuf[idPos + 1] == 0xA1 &&
               _mfmTrackBuf[idPos + 2] == 0xA1 && _mfmTrackBuf[idPos + 3] == 0xFE))
        idPos++;
      if (idPos + 10 > MFM_TRACK_BYTES) break;

      uint32_t dataMark = idPos + 10;
      const uint32_t searchEnd = dataMark + 256 < MFM_TRACK_BYTES ? dataMark + 256 : MFM_TRACK_BYTES;
      while (dataMark + 4 <= searchEnd &&
             !(_mfmTrackBuf[dataMark] == 0xA1 && _mfmTrackBuf[dataMark + 1] == 0xA1 &&
               _mfmTrackBuf[dataMark + 2] == 0xA1 &&
                (_mfmTrackBuf[dataMark + 3] == 0xFB || _mfmTrackBuf[dataMark + 3] == 0xF8)))
        dataMark++;
      const uint8_t n = _mfmTrackBuf[idPos + 7];
      if (n > 6) { pos = dataMark + 4; continue; }
      const uint16_t length = (uint16_t)(128u << n);
      if (dataMark + 4 > searchEnd) {
        // Preserve an ID field with a missing data address mark. The 8272
        // must still answer READ ID, while READ DATA returns ST1=NO DATA.
        const uint8_t slot = tr.sectorCount++;
        uint8_t *id = _trackBuf + slot * 8;
        id[0] = _mfmTrackBuf[idPos + 4];
        id[1] = _mfmTrackBuf[idPos + 5];
        id[2] = _mfmTrackBuf[idPos + 6];
        id[3] = n;
        id[4] = 0x04;
        id[5] = 0;
        id[6] = id[7] = 0;
        _trackDataOffset[slot] = 0;
        _trackDataLength[slot] = 0;
        pos = idPos + 10;
        continue;
      }
      const uint32_t dataOffset = dataMark + 4;
      if (dataOffset + length + 2 > MFM_TRACK_BYTES) return false;

      const uint8_t slot = tr.sectorCount++;
      uint8_t *id = _trackBuf + slot * 8;
      id[0] = _mfmTrackBuf[idPos + 4];
      id[1] = _mfmTrackBuf[idPos + 5];
      id[2] = _mfmTrackBuf[idPos + 6];
      id[3] = n;
      id[4] = 0;
      id[5] = _mfmTrackBuf[dataMark + 3] == 0xF8 ? 0x40 : 0;
      id[6] = (uint8_t)length;
      id[7] = (uint8_t)(length >> 8);
      _trackDataOffset[slot] = ti + dataOffset;
      _trackDataLength[slot] = length;
      pos = dataOffset + length + 2;
    }
    tr.sizeCode = 1;
    return tr.sectorCount > 0;
  }
  if ((_is[drive])->seek((uint32_t)ti) < 0) return false;
  uint8_t h[248];
  uint32_t got = (_is[drive])->read(h, 248);
  if (got < 24 || memcmp(h, TRACK_MAGIC, 12) != 0) return false;
  uint8_t count = h[21] > 28 ? 28 : h[21];
  if (got < (uint32_t)(24 + count * 8)) count = (got - 24) / 8;
  tr.sectorCount = count;
  tr.sizeCode = h[20];
  memcpy(_trackBuf, h + 24, count * 8);
  return count > 0;
}

int ZxSpectrumHcFdc::findSector(int drive, bool sequential, Sector &out) {
  if (!loadTrackTable(drive, _side, _tr)) return -1;
  int matchIdx = -1;
  if (sequential) {
    if (_trackCount < _tr.sectorCount) matchIdx = _trackCount;
  } else {
    // The 8272 matches a sector by H, R. A wrong C is reported in ST2 (bit 4,
    // bad-cylinder bit 1 when the id field reads C=0xff), not a hard fail
    // (reference Fdc8272.Search).
    for (int s = 0; s < _tr.sectorCount; s++) {
      const uint8_t *id = _trackBuf + s * 8;
      if (id[1] == _h && id[2] == _r) {
        if (id[0] == _c) { matchIdx = s; break; }
        _st2 |= (uint8_t)(0x10 | (id[0] == 0xff ? 2 : 0));
      }
    }
    if (matchIdx < 0) {
      // Reference leniency (Fdc8272.Search): the CP/M BDOS on this disk pack
      // uses a 16-sector skew but tracks hold 9 (or fewer) sectors. Requests
      // for R beyond the last sector, up to R=16, return the last sector's
      // data with the ID of the sector actually read.
      int maxRi = 0;
      for (int s = 1; s < _tr.sectorCount; s++)
        if (_trackBuf[s * 8 + 2] > _trackBuf[maxRi * 8 + 2]) maxRi = s;
      if (_r > _trackBuf[maxRi * 8 + 2] && _r <= 16) {
        matchIdx = maxRi;
        _r = _trackBuf[maxRi * 8 + 2];
      }
    }
  }
  if (matchIdx < 0) return -1;
  const uint8_t *id = _trackBuf + matchIdx * 8;
  uint16_t len = id[6] | (id[7] << 8);
  if (!len && !_extFmt[drive]) len = (uint16_t)(128 << (_tr.sizeCode & 7));
  if (!len) len = (uint16_t)(128 << (id[3] & 7));
  // EDSK stores sector data contiguously inside the track block
  // (Dsk.cs: position += size), no per-sector padding.
  uint32_t off;
  if (_mfmFmt[drive]) {
    off = _trackDataOffset[matchIdx];
    len = _trackDataLength[matchIdx];
  } else {
    off = (uint32_t)_tr.base + 256;
    for (int s = 0; s < matchIdx; s++) {
      uint16_t l = _trackBuf[s * 8 + 6] | (_trackBuf[s * 8 + 7] << 8);
      if (!l && !_extFmt[drive]) l = (uint16_t)(128 << (_tr.sizeCode & 7));
      if (!l) l = (uint16_t)(128 << (_trackBuf[s * 8 + 3] & 7));
      off += l;
    }
  }
  out.c = id[0]; out.h = id[1]; out.r = id[2]; out.n = id[3];
  out.st1 = id[4]; out.st2 = id[5];
  out.imgOff = off;
  out.len = len > MEDIA_MAX ? MEDIA_MAX : len;
  return matchIdx;
}

bool ZxSpectrumHcFdc::startBufferedSector(const Sector &s) {
  if (!_is[_activeDrive]) return false;
  if ((_is[_activeDrive])->seek((uint32_t)s.imgOff) < 0) return false;
  uint32_t want = s.len < MEDIA_MAX ? s.len : MEDIA_MAX;
  uint32_t got = (_is[_activeDrive])->read(_media, want);
  return got == want;
}

bool ZxSpectrumHcFdc::driveUsable(int unit) {
  // TIM mode: the boot EPROM never writes $1FFD (the only 1FFD constant in the
  // 2 KiB dump feeds the vector stub at $000A), so the motor latch can never
  // arm reads if gated on it - every READ returns ST1=04 no-data and the menu
  // "loads nothing". Drive presence alone usability (the motherboard FDC unit
  // select happens through the command byte, and the motor line stays
  // informational via setTimsMotor for later CP/M drivers).
  if (_timsMode && unit >= 0 && unit <= 1 && mediaPresent(unit)) return true;
  if (_plus3Mode && unit >= 0 && unit <= 1 && mediaPresent(unit) && _motorOn) return true;
  if (unit == 0 && (_latch & 2) && mediaPresent(0) && _motorOn) return true;
  if (unit == 1 && (_latch & 4) && mediaPresent(1) && _motorOn) return true;
  return false;
}

void ZxSpectrumHcFdc::execute() {
  trace('C', _cmd, _cmdLen);
  uint8_t pc[2] = { (uint8_t)_cmdPc, (uint8_t)(_cmdPc >> 8) };
  trace('P', pc, 2);
  _cmdPos = _cmdLen = 0;
  switch (opcode()) {
    case 3: // Specify
      return; // stays in PH_COMMAND
    case 4: { // Sense drive status
      sdCmds++; // diag: SENSE DRIVE STATUS commands issued
      _unit = _cmd[1] & 3;
      _side = (_cmd[1] >> 2) & 1;
      int drive = _timsMode
        // TIM mode: presence only - the boot EPROM never arms the $1FFD motor
        // latch, so a motor-gated check would report "no drive" (see driveUsable).
        ? (_unit <= 1 && mediaPresent(_unit) ? _unit : -1)
        : _plus3Mode
          ? (_unit <= 1 && mediaPresent(_unit) && _motorOn ? _unit : -1)
          : (_unit == 0 && (_latch & 2) ? 0 : _unit == 1 && (_latch & 4) ? 1 : -1);
      // ST3: b0/b1 unit code, b2 head, b3 two-sided, b4 track-zero after a
      // recalibrate. TIM diag4: ready (b5, 0x20) follows driveUsable() - in
      // TIM mode that is presence-only (the boot EPROM never arms the $1FFD
      // motor latch, so a motor-gated ready would read "not ready"). The
      // +3/HC2000 paths keep the historical always-ready result byte.
      // Write-protect (b6) and fault (b7) stay 0 for mounted DSK media.
      uint8_t st3 = (uint8_t)(_unit | (_side << 2));
      if (!_timsMode || driveUsable(_unit)) st3 |= 0x20;
      if (drive >= 0) {
        if (_pcn[drive] == 0) st3 |= 0x10;
        if (_sideCount[drive] == 2) st3 |= 8;
      }
      lastSt3 = st3; // diag: last SENSE DRIVE STATUS result byte
      setSingleResult(st3);
      return;
    }
    case 8: // Sense interrupt status
      if (_intCount > 0) {
        _result[0] = _intStatus[0];
        _result[1] = _intCyl[0];
        _resultCount = 2;
        _intCount--;
        for (uint8_t i = 0; i < _intCount; i++) {
          _intStatus[i] = _intStatus[i + 1];
          _intCyl[i] = _intCyl[i + 1];
        }
      } else {
        setSingleResult(0x80);
        return;
      }
      _resultPos = 0;
      _phase = PH_RESULT;
      return;
    case 7: case 0x0f: // Recalibrate / Seek
      seekStart();
      return;
    case 1: case 2: case 5: case 6: case 9: case 0x0a: case 0x0c:
    case 0x11: case 0x19: case 0x1d:
      beginTransfer();
      return;
    default:
      setSingleResult(0x80);
      return;
  }
}

void ZxSpectrumHcFdc::setSingleResult(uint8_t st0) {
  trace('R', &st0, 1);
  _result[0] = st0;
  _resultCount = 1; _resultPos = 0;
  _phase = PH_RESULT;
}

void ZxSpectrumHcFdc::seekStart() {
  int unit = _cmd[1] & 3;
  uint8_t side = (_cmd[1] >> 2) & 1;
  uint8_t target = opcode() == 7 ? 0 : _cmd[2]; // recalibrate: track 0
  if (unit > 1) return;
  if (!_loaded[unit]) {
    // Reference SeekStep with null drive: head never settles, mark failed.
    _pcn[unit] = 0;
    if (_intCount < 8) {
      _intCyl[_intCount] = 0;
      _intStatus[_intCount] = (uint8_t)(0x20 | unit | (side << 2) | 0x50);
      _intCount++;
    }
    return;
  }
  while (_pcn[unit] != target) {
    if (_pcn[unit] < target) _pcn[unit]++;
    else if (_pcn[unit] > target) _pcn[unit]--;
  }
  if (_intCount < 8) {
    _intCyl[_intCount] = _pcn[unit];
    _intStatus[_intCount] = (uint8_t)(0x20 | unit | (side << 2));
    _intCount++;
  }
}

void ZxSpectrumHcFdc::beginTransfer() {
  _unit = _cmd[1] & 3;
  _side = (_cmd[1] >> 2) & 1;
  _st1 = _st2 = 0;
  _terminal = false;
  _byteReady = false;
  _trackCount = 0;
  _scanEq = _scanSat = true;
  _c = opcode() == 0x0a ? _pcn[_unit] : _cmd[2];
  _h = opcode() == 0x0a ? _side : _cmd[3];
  _r = opcode() == 0x0a ? 1 : _cmd[4];
  _n = opcode() == 0x0a ? 0 : _cmd[5];
  _eot = opcode() == 0x0a ? 1 : _cmd[6];

  if (!driveUsable(_unit)) { finish(4, 0); return; }
  int drive = _unit == 0 ? 0 : 1;
  _activeDrive = drive;
  _phase = PH_EXEC;
  if (_side >= _sideCount[drive]) { finish(4, 0); return; }
  if (_n > 6) { finish(4, 0); return; }
  if (opcode() == 0x0a) {
    Track tr;
    if (!loadTrackTable(drive, _side, tr) || tr.sectorCount == 0) { finish(1, 0); return; }
    _c = _trackBuf[0]; _h = _trackBuf[1]; _r = _trackBuf[2]; _n = _trackBuf[3];
    finish(0, 0);
    return;
  }
  if (writesMedia()) { finish(2, 0); return; } // mounted media is read-only
  startLoadedSector();
}

void ZxSpectrumHcFdc::startLoadedSector() {
  Sector s;
  bool sequential = opcode() == 2;
  if (findSector(_activeDrive, sequential, s) < 0) { finish(sequential ? 0x80 : 4, _st2); return; }
  if (!startBufferedSector(s)) { finish(4, _st2); return; }
  _sector = s;
  _c = s.c; _h = s.h; _r = s.r; _n = s.n;
  lastC = s.c; lastH = s.h; lastR = s.r; lastN = s.n; // diag: sector find
  _st1 |= (uint8_t)(s.st1 & 0x25);
  _st2 |= (uint8_t)(s.st2 & 0x21);
  if (_st1 & 0x05) { finish(_st1, _st2); return; }
  _xferLen = s.len;
  _byteIndex = 0;
  if (_trackCount == 0) {
    uint8_t s6[6] = { s.c, s.h, s.r, s.n, (uint8_t)s.len, (uint8_t)(s.len >> 8) };
    trace('S', s6, 6);
  }
  _byteReady = !_terminal;
}

void ZxSpectrumHcFdc::advanceByte(bool fromDataRead) {
  (void)fromDataRead;
  if (_activeDrive < 0 || !mediaPresent(_activeDrive)) { finish(4, 0); return; }
  // Reference AdvanceInstantBytes: once the BIOS pulses TC the remaining
  // bytes drain instantly and the sector (then the command) completes.
  if (_terminal)
    while (_byteIndex < _xferLen) _byteIndex++;
  if (_byteIndex >= _xferLen) { completeSector(); return; }
  _byteReady = true;
}

void ZxSpectrumHcFdc::advanceId() {
  if (_r >= _eot) {
    if ((_cmd[0] & 0x80) == 0 || _side == 1) _c++;
    if ((_cmd[0] & 0x80) != 0) _h ^= 1;
    _r = 1;
  } else {
    _r++;
  }
}

void ZxSpectrumHcFdc::completeSector() {
  if (_r >= _eot || opcode() == 2) { trace('D', &_r, 1); }
  if (scanning()) {
    if (_scanSat) { finish(_st1, _scanEq ? (uint8_t)(_st2 | 8) : _st2); return; }
    if (_terminal || _r >= _eot) { finish(_st1, _st2 | 4); return; }
  }
  if (_st1 != 0 || _st2 != 0) { finish(_st1, _st2); return; }
  if (_terminal) {
    advanceId();
    finish(0, 0);
    return;
  }
  _trackCount++;
  if (opcode() == 2) {
    if (_trackCount >= _eot) { finish(0x80, _st2); return; }
  } else if (_r >= _eot) {
    if ((_cmd[0] & 0x80) != 0 && _side == 0) {
      _side = 1; _h = 1; _r = 1;
      } else if (_timsMode) {
        // No TC line in TIM mode: real 8272 ends READ DATA at end-of-track
        // with ST0 abnormal (0x40|unit|head), ST1 EN (0x80), CHRN unchanged.
        finish((uint8_t)(_st1 | 0x80), _st2);
        return;
      } else {
        advanceId();
        finish(_plus3Mode ? _st1 : 0x80, _st2);
        return;
    }
  } else {
    _r++;
  }
  startLoadedSector();
}

void ZxSpectrumHcFdc::finish(uint8_t st1, uint8_t st2) {
  uint8_t st0 = (uint8_t)(_unit | (_side << 2) | ((st1 != 0 || (st2 & 0xF7) != 0) ? 0x40 : 0));
  lastSt0 = st0; // diag
  if ((st1 & 0x04) != 0) noDataErr++;      // diag: ST1 no-data
  if ((st2 & 0x21) != 0) crcErr++;         // diag: data-field mark/CRC issue
  uint8_t r7[7];
  r7[0] = st0; r7[1] = st1; r7[2] = st2; r7[3] = _c; r7[4] = _h; r7[5] = _r; r7[6] = _n;
  trace('R', r7, 7);
  _result[0] = st0;
  _result[1] = st1;
  _result[2] = st2;
  _result[3] = _c;
  _result[4] = _h;
  _result[5] = _r;
  _result[6] = _n;
  _resultCount = 7;
  _resultPos = 0;
  _cmdPos = _cmdLen = 0;
  _byteReady = false;
  _activeDrive = -1;
  _phase = PH_RESULT;
}
