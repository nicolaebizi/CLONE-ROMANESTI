// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#include "ZxSpectrumCf.h"
#include <string.h>

const char* ZxSpectrumCf::IMAGE_PATH = "zxspectrum/cf/SUPERSPEC.CF";

// Virtual image size: 3,670,000,000 bytes (~3.67 GB). Root cause (hardware
// confirmed): Koning's FS = ROOT (LBA 1024) + 1000 DIR blocks x 7168 sectors
// = ~3.5 GB of structure; a 16 MB image made every read past its end fail,
// which the ROM turned into its "press a key" error loop at LBA 0x202020
// (DIR #293). The file is grown to its full size with METADATA-ONLY FatFs
// calls (no 0xFF fill writes, no contiguity needed) and the uniform-write
// table carries the 0x20/0xFF content virtually, so the SD card never sees
// the FORMAT! traffic.
const uint64_t ZxSpectrumCf::IMAGE_BYTES = 3670000000ull;

// Fallback auto-created image: 16 MB of 0xFF, only for cards too small or
// too fragmented for the 3.67 GB contiguous preallocation.
const uint32_t ZxSpectrumCf::BLANK_IMAGE_BYTES = 16u * 1024u * 1024u;

// Core1-visible creation progress (see ZxSpectrumCf.h): volatile because the
// VGA render loop on core1 samples them while core0 writes the image.
volatile uint32_t zxCfImgMaking = 0;
volatile uint32_t zxCfImgDone = 0;

ZxSpectrumCf::ZxSpectrumCf(SdCardFatFsSpi* sdCard) :
  _sdCard(sdCard),
  _open(false),
  _writable(false),
  _idx(0),
  _bufSects(0),
  _sectLeft(0),
  _lbaReg(0),
  _curLba(0),
  _chunkEnd(0),
  _countReg(0),
  _status(CF_ST_IDLE),
  _diagCmd(0),
  _diagLba(0),
  _diagCount(0),
  _nCmd(0),
  _nWr(0),
  _nRd(0),
  _nErr(0),
  _imgMaking(false),
  _imgDone(0)
{
  memset(_uwt, 0, sizeof(_uwt));
  _uwtN = 0;
  _imgBytes = 0;
  _cmdDirty = false;
}

// Machine reset: registers cleared, the image file closed so the next CF
// access re-opens it (a SUPERSPEC.CF copied to the SD after boot is picked
// up on the next reset). Media state is re-evaluated lazily. The diag
// counters are cleared too: they describe the traffic of one DOS session.
// The uniform-write table is NOT cleared here: it describes the image file
// content, which survives the reset exactly like the file does; it is only
// dropped when a file of a different size is opened (see noteImageSize).
void ZxSpectrumCf::reset() {
  if (_open) { f_close(&_fil); _open = false; _writable = false; }
  _idx = 0;
  _bufSects = 0;
  _sectLeft = 0;
  _lbaReg = 0;
  _curLba = 0;
  _chunkEnd = 0;
  _countReg = 0;
  _status = CF_ST_IDLE;
  _diagCmd = 0;
  _diagLba = 0;
  _diagCount = 0;
  _nCmd = 0;
  _nWr = 0;
  _nRd = 0;
  _nErr = 0;
  _imgMaking = false;
  _imgDone = 0;
  zxCfImgMaking = 0;
  zxCfImgDone = 0;
}

void ZxSpectrumCf::diag(CfDiag* d) const {
  d->lba = _diagLba;
  d->count = (uint8_t)_diagCount;
  d->status = _status;
  d->cmd = _diagCmd;
  d->nCmd = _nCmd;
  d->nWr = _nWr;
  d->nRd = _nRd;
  d->nErr = _nErr;
  // FT = uniform-write table entries in use (virtual-format overlay load).
  uint32_t used = 0;
  for (uint16_t i = 0; i < _uwtN; ++i) if (_uwt[i].used) ++used;
  d->nUwt = used;
  d->imgMaking = _imgMaking;
  d->imgDone = _imgDone;
}

// Lazy open at the first CF access (CobraDiskFile pattern): the image is
// opened read/write when the FatFs layer allows it, read-only otherwise
// (write command then reports ERR, like a write-protected card). A missing
// file leaves _open false and latches the ERR status - no media.
bool ZxSpectrumCf::ensureOpen() {
  if (_open) return true;
  if (_sdCard == 0) { fail(); return false; }
  if (!_sdCard->mounted() && !_sdCard->mount()) { fail(); return false; }
  if (f_open(&_fil, IMAGE_PATH, FA_READ | FA_WRITE | FA_OPEN_EXISTING) == FR_OK) {
    _open = true;
    _writable = true;
    noteImageSize();
    if (_status == CF_ST_ERR) _status = CF_ST_IDLE;
    return true;
  }
  if (f_open(&_fil, IMAGE_PATH, FA_READ | FA_OPEN_EXISTING) == FR_OK) {
    _open = true;
    _writable = false;
    noteImageSize();
    if (_status == CF_ST_ERR) _status = CF_ST_IDLE;
    return true;
  }
  // No image file at all: auto-create a preallocated 3.67 GB image (the
  // erased-state view comes from the uniform-write table and from reads
  // beyond the physical content - no 0xFF fill is ever written) and re-open
  // it. Still no luck (write-protected mount, no folder) -> no-media ERR.
  if (createBlankImage() &&
      f_open(&_fil, IMAGE_PATH, FA_READ | FA_WRITE | FA_OPEN_EXISTING) == FR_OK) {
    _open = true;
    _writable = true;
    noteImageSize();
    if (_status == CF_ST_ERR) _status = CF_ST_IDLE;
    return true;
  }
  fail();
  return false;
}

// Refresh the cached backing-file size. The size identifies the image: a
// different size than the table was built for means a different SUPERSPEC.CF
// (e.g. a dump copied over between resets) - drop the uniform-write table so
// stale virtual content can never shadow the new file.
void ZxSpectrumCf::noteImageSize() {
  const uint64_t sz = f_size(&_fil);
  if (sz != _imgBytes) {
    memset(_uwt, 0, sizeof(_uwt));
    _uwtN = 0;
    _imgBytes = sz;
  }
}

// One-time image creation, FAST metadata-only growth: the file is created
// (FA_CREATE_ALWAYS, always fresh) and grown to 3,670,000,000 bytes with
// f_lseek(end) + f_truncate(end). In this FatFs build (pimoroni ffconf.h:
// FF_FS_MINIMIZE=0, FF_USE_EXPAND=1) an f_lseek PAST EOF on a write-mode
// file stretches the cluster chain via create_chain(forced) - FAT/exFAT
// metadata writes only, NO data sectors, NO contiguity needed - and sets
// the file size to the new offset (ff.c f_lseek, "Follow chain with forced
// stretch" + the trailing objsize update); f_truncate at the end offset is
// a no-op that just confirms the write access. Instant on any card with
// enough free clusters, fragmented or not. The 0xFF erased content comes
// from the uniform-write table and from reads beyond the file's written
// data, never from the SD.
// SANITY CHECK: on a disk-full card f_lseek silently clips the chain
// (create_chain == 0 -> ofs = 0, break) and STILL returns FR_OK, so the
// real test is the resulting f_size() == IMAGE_BYTES. Anything smaller (or
// any FatFs error) removes the partial file, latches the diag E counter
// (menu shows E=1, FT stays 0) and falls back to the old 16 MB 0xFF fill -
// degraded, but boots. On a mid-fallback failure the partial file is
// removed so the next reset retries. (History: f_expand needed ONE
// CONTIGUOUS free area and failed on the user's fragmented/used card, so
// the 16 MB fallback ran and reads past its end failed forever - LBA202020
// R0 FT0 on hardware.)
bool ZxSpectrumCf::createBlankImage() {
  FIL fil;
  _imgMaking = true;
  _imgDone = 0;
  zxCfImgMaking = 1;
  zxCfImgDone = 0;
  if (f_open(&fil, IMAGE_PATH, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
    _imgMaking = false;
    zxCfImgMaking = 0;
    return false;
  }
  const bool grown =
    f_lseek(&fil, (FSIZE_t)IMAGE_BYTES) == FR_OK &&
    f_truncate(&fil) == FR_OK &&
    f_size(&fil) == (FSIZE_t)IMAGE_BYTES;
  f_close(&fil);
  if (!grown) {
    f_unlink(IMAGE_PATH);
    fail(); // diag E latches the failed 3.67 GB growth (FT stays 0)
    const bool ok = createSmallImage();
    _imgMaking = false;
    zxCfImgMaking = 0;
    return ok;
  }
  _imgDone = (uint32_t)IMAGE_BYTES;
  zxCfImgDone = _imgDone;
  _imgMaking = false;
  zxCfImgMaking = 0;
  return true;
}

// Fallback for cards without enough free clusters for the 3.67 GB growth
// (disk full/too small - the f_lseek+f_size sanity check caught it): the
// old 16 MB 0xFF fill, 4K chunks (no stack use). The chunk buffer reuses
// the transfer buffer _buf: it only ever runs before the first CF command
// is armed (ensureOpen -> status ERR), so no transfer data can live in
// _buf yet.
bool ZxSpectrumCf::createSmallImage() {
  uint8_t* ffBuf = _buf;
  const uint32_t ffLen = sizeof(_buf);
  memset(ffBuf, 0xFF, ffLen);
  FIL fil;
  if (f_open(&fil, IMAGE_PATH, FA_WRITE | FA_CREATE_NEW) != FR_OK) return false;
  bool ok = true;
  for (uint32_t i = 0; ok && i < BLANK_IMAGE_BYTES / ffLen; ++i) {
    UINT bw = 0;
    ok = f_write(&fil, ffBuf, ffLen, &bw) == FR_OK && bw == ffLen;
    _imgDone = (uint32_t)(i + 1) * ffLen;
    zxCfImgDone = _imgDone;
  }
  if (ok) ok = f_sync(&fil) == FR_OK;
  f_close(&fil);
  if (!ok) f_unlink(IMAGE_PATH);
  return ok;
}

uint8_t ZxSpectrumCf::readReg(uint8_t idx) {
  ensureOpen();
  if (idx == 7) return _status; // STATUS / COMMAND register read
  if (idx == 0) return dataRead();
  if (idx == 1) return dataPeek();
  return regRead(idx);
}

void ZxSpectrumCf::writeReg(uint8_t idx, uint8_t value) {
  ensureOpen();
  if (idx == 7) { // command register
    if (value == CMD_READ || value == CMD_WRITE) issue(value);
    return; // other commands (idle 0x90 etc.) keep the current status
  }
  if (idx == 0) { dataWrite(value); return; }
  if (idx == 1) { // data-odd write: store at the current pointer, no advance
    if (_status == CF_ST_DRQ && _idx < 512) _buf[_bufSects * 512 + _idx] = value;
    return;
  }
  regWrite(idx, value);
}

// Command 0x20/0x30: arm the transfer at the latched LBA with the latched
// sector count (0 written means 256, ATA semantics). DRQ (=0x58, bit3 set -
// the DOS's "more data" test) goes up synchronously; a failing backing store
// latches ERR and the DOS times out into its sector error.
void ZxSpectrumCf::issue(uint8_t cmd) {
  ++_nCmd;
  _diagCmd = cmd;
  _diagCount = _countReg;
  _nWr = 0; // diag W counts the bytes of the command being armed
  _cmdDirty = false; // f_sync is skipped at the end if nothing physical happened
  // 0 written to the sector count means 256 (ATA semantics).
  const uint32_t n = _countReg ? _countReg : 256;
  _curLba = _lbaReg & 0x0FFFFFFFu; // LBA28, the |0xE0 head bits are masked off
  _diagLba = _curLba;
  _idx = 0;
  _bufSects = 0;
  _sectLeft = (uint16_t)(n - 1);
  if (cmd == CMD_READ) {
    if (!_open) { _sectLeft = 0; fail(); return; }
    // One f_lseek + one f_read per buffer chunk (8 sectors / 4K), not one
    // per sector: LOAD streams up to 256 sectors with a
    // poll between each, and the DIR scan loops read one sector per DIR.
    _chunkEnd = _curLba;
    if (loadChunk()) {
      _status = CF_ST_DRQ;
    }
    else {
      _sectLeft = 0;
      fail();
    }
    return;
  }
  // WRITE SECTORS: seek to the transfer start ONCE per command; the data
  // register writes fill _buf, chunk writes are either recorded in the
  // uniform-write table (no SD traffic at all) or appended with sequential
  // f_write calls, and f_sync runs once at the end - and only if something
  // physical was actually written.
  if (!_open || !_writable) { _sectLeft = 0; fail(); return; }
  // LBA beyond the backing image: ERR, like a real CF card asked for a
  // sector outside its geometry (the DOS never goes past ~3.5 GB).
  if ((uint64_t)_curLba * 512 >= _imgBytes) { _sectLeft = 0; fail(); return; }
  if (f_lseek(&_fil, (FSIZE_t)_curLba * 512) != FR_OK) {
    _sectLeft = 0;
    fail();
    return;
  }
  _status = CF_ST_DRQ;
}

// Read the next chunk (up to CF_BUF_SECTS sectors starting at _curLba) of an
// active read transfer, resolving every sector against the uniform-write
// table first: a hit fabricates the recorded byte pattern (no SD access -
// this is what makes the DIR scans after FORMAT! instant and what serves
// the reads the ROM does of areas the FORMAT! only virtually cleared); a
// miss reads the real file, with consecutive misses grouped into ONE
// f_lseek+f_read; an LBA beyond the backing file fabricates 0xFF (the
// erased state - the old short-image 0xFF pad, now also covering reads
// past a real card's geometry instead of failing). The chunk always starts
// at the sector being streamed, so _buf[_idx] with the running _idx is the
// stream position. Returns false (-> ERR status) on any FatFs failure.
bool ZxSpectrumCf::loadChunk() {
  const uint16_t n = (uint16_t)((_sectLeft + 1 < CF_BUF_SECTS) ? _sectLeft + 1 : CF_BUF_SECTS);
  uint16_t k = 0;
  while (k < n) {
    const uint32_t lba = _curLba + k;
    const int hit = uwtLookup(lba);
    if (hit >= 0) {
      memset(_buf + (uint32_t)k * 512, (uint8_t)hit, 512);
      ++k;
      continue;
    }
    if ((uint64_t)lba * 512 >= _imgBytes) {
      memset(_buf + (uint32_t)k * 512, 0xFF, 512);
      ++k;
      continue;
    }
    // Real read: take the longest run of misses up to the next table hit or
    // the image end (usually the whole chunk = one f_lseek + one f_read).
    uint16_t m = 1;
    while (k + m < n && uwtLookup(lba + m) < 0 && (uint64_t)(lba + m) * 512 < _imgBytes) ++m;
    if (f_lseek(&_fil, (FSIZE_t)lba * 512) != FR_OK) return false;
    UINT br = 0;
    if (f_read(&_fil, _buf + (uint32_t)k * 512, (UINT)m * 512, &br) != FR_OK) return false;
    while (br < (UINT)m * 512) _buf[(uint32_t)k * 512 + br++] = 0xFF;
    k += m;
  }
  _bufSects = n;
  _chunkEnd = _curLba + n;
  _idx = 0;
  return true;
}

// Flush the pending chunk of a write transfer. THE VIRTUAL-FORMAT CORE: if
// every byte of the pending sectors is the same value (the DOS's FORMAT!
// clears ROOT + 1000 DIR blocks with count=1 commands of uniform 0x20, and
// its file-area erases use uniform 0xFF), the chunk is NOT written to the
// SD at all - it is recorded in the uniform-write table (appended /
// coalesced into the previous run when adjacent, carved-out invalidation
// happens on the real-write path only, so a same-value overlap can leave a
// harmless duplicate). A table-full record falls back to a real f_write of
// the data still sitting in _buf (safe degradation). Real data goes through
// with ONE f_write, and any recorded range it overlaps is split so the real
// data wins on subsequent reads. On failure the transfer ends with ERR -
// the DOS's tstmult/cfexit then raises sector error.
bool ZxSpectrumCf::writeChunk() {
  if (_bufSects == 0) return true;
  const uint32_t start = _curLba - _bufSects; // sectors completed sequentially
  const uint16_t n = _bufSects;
  const UINT bytes = (UINT)n * 512;
  const uint8_t v0 = _buf[0];
  bool uniform = true;
  for (UINT i = 1; i < bytes; ++i) {
    if (_buf[i] != v0) { uniform = false; break; }
  }
  _bufSects = 0;
  if (uniform && uwtAppend(start, n, v0)) return true; // virtual: no SD traffic
  // Real write: the pending data must be seeked explicitly - virtual chunks
  // above did not advance the file pointer, and mid-command chunk writes
  // must land at their own LBA regardless.
  if (f_lseek(&_fil, (FSIZE_t)start * 512) != FR_OK) {
    _sectLeft = 0;
    _idx = 0;
    _bufSects = 0;
    fail();
    return false;
  }
  UINT bw = 0;
  const bool ok = f_write(&_fil, _buf, bytes, &bw) == FR_OK && bw == bytes;
  if (!ok) {
    _sectLeft = 0;
    _idx = 0;
    _bufSects = 0;
    fail();
    return false;
  }
  _cmdDirty = true;
  // With _buf free again: carve the real-write range out of the table (a
  // split that cannot be recorded - table full - persists the lost
  // remainder through uwtWriteOut so reads stay correct).
  uwtInvalidate(start, n);
  return true;
}

// End of a write command: flush the pending chunk, then ONE f_sync for the
// whole transfer (this is the expensive call - once per command instead of
// once per 512B sector; skipped entirely when the command was served from
// the uniform-write table and touched no file data). On success the status
// drops to IDLE 0x50 HERE: the status register is what the ROM polls after
// the last data byte, and a stale DRQ 0x58 makes its retry loop re-issue
// the same WRITE command forever (seen on hardware as LBA frozen + W
// advancing in 512B steps).
bool ZxSpectrumCf::flushFinal() {
  if (!writeChunk()) return false;
  if (_cmdDirty && f_sync(&_fil) != FR_OK) {
    fail();
    _sectLeft = 0;
    _idx = 0;
    return false;
  }
  _status = CF_ST_IDLE;
  return true;
}

// DATA register (reg 0): one byte per strobe, pointer advances. At the
// sector boundary: stream the next sector while count remains, otherwise the
// transfer ends (status back to idle 0x50, bit3 = 0 - the DOS's tstmult /
// bank-0 poll then terminates). The next sector is usually already in _buf
// (multi-sector chunk; _idx runs 0..chunkBytes across sectors) and crossing
// a chunk boundary costs one f_read that resets _idx to 0.
uint8_t ZxSpectrumCf::dataRead() {
  if (_status != CF_ST_DRQ || _idx >= (uint16_t)(_bufSects * 512)) return 0xFF;
  const uint8_t v = _buf[_idx++];
  if (_idx % 512 == 0) {
    if (_sectLeft > 0) {
      ++_curLba;
      --_sectLeft;
      if (_curLba >= _chunkEnd) {
        if (!loadChunk()) {
          // mid-transfer read failure: ERR, not IDLE (the DOS's tstmult
          // drops into cfexit and reports sector error instead of silently
          // using garbage)
          _sectLeft = 0;
          _idx = 0;
          fail();
        }
      }
    }
    else {
      _sectLeft = 0;
      _idx = 0;
      _status = CF_ST_IDLE;
    }
  }
  return v;
}

// DATA-odd (reg 1): peek the current byte without advancing (the "byte can
// be read twice from reg 0" behaviour of the 16-bit data register).
uint8_t ZxSpectrumCf::dataPeek() {
  if (_status != CF_ST_DRQ || _idx >= (uint16_t)(_bufSects * 512)) return 0xFF;
  return _buf[_idx];
}

void ZxSpectrumCf::dataWrite(uint8_t value) {
  if (_status != CF_ST_DRQ || _idx >= 512) return;
  _buf[_bufSects * 512 + _idx] = value;
  ++_idx;
  if (_idx < 512) return;
  // Sector boundary: the sector just completed is buffered; chunk writes
  // append to the file WITHOUT f_sync, the end of the command does one.
  _idx = 0;
  ++_bufSects;
  _nWr += 512; // diag W: bytes of the CURRENT command (reset in issue())
  if (_sectLeft > 0) {
    --_sectLeft;
    ++_curLba;
    if (_bufSects == CF_BUF_SECTS && !writeChunk()) return;
    return; // stay DRQ: the ROM's bit-3 poll sees "more data"
  }
  // Last sector of the command: flush + single f_sync, back to idle.
  _sectLeft = 0;
  ++_curLba; // ATA behaviour: the LBA register ends past the written sector
  _diagLba = _curLba;
  flushFinal(); // sets IDLE on success, ERR on failure
  _idx = 0;
  _bufSects = 0;
}

uint8_t ZxSpectrumCf::regRead(uint8_t idx) {
  switch (idx) {
    case 2: return _countReg;
    case 3: return (uint8_t)(_lbaReg & 0xFF);
    case 4: return (uint8_t)((_lbaReg >> 8) & 0xFF);
    case 5: return (uint8_t)((_lbaReg >> 16) & 0xFF);
    case 6: return (uint8_t)((_lbaReg >> 24) & 0xFF);
    default: return 0xFF;
  }
}

void ZxSpectrumCf::regWrite(uint8_t idx, uint8_t value) {
  switch (idx) {
    case 2: _countReg = value; break;
    case 3: _lbaReg = (_lbaReg & 0xFFFFFF00u) | value; break;
    case 4: _lbaReg = (_lbaReg & 0xFFFF00FFu) | ((uint32_t)value << 8); break;
    case 5: _lbaReg = (_lbaReg & 0xFF00FFFFu) | ((uint32_t)value << 16); break;
    case 6: _lbaReg = (_lbaReg & 0x00FFFFFFu) | ((uint32_t)value << 24); break; // |0xE0 head bits masked at use
    default: break;
  }
}

// ---- uniform-write table (virtual-format overlay) ------------------------

// Record a uniform sector run: extend the previous adjacent same-value run
// when possible (Koning's FORMAT! clears 1000 DIR blocks sequentially, so
// the whole ~3.5 GB of 0x20 coalesces into few entries; a run longer than a
// u16 count starts a new entry), skip an exact duplicate, otherwise append.
// Returns false when the table is full -> the caller falls back to a real
// f_write of the data it still holds.
bool ZxSpectrumCf::uwtAppend(uint32_t startLba, uint32_t count, uint8_t val) {
  if (count == 0 || count > 0xFFFFu) return false;
  for (uint16_t i = 0; i < _uwtN; ++i) {
    if (!_uwt[i].used || _uwt[i].val != val) continue;
    if (_uwt[i].startLba == startLba && _uwt[i].count == count) return true;
    if (_uwt[i].startLba + _uwt[i].count == startLba &&
        (uint32_t)_uwt[i].count + count <= 0xFFFFu) {
      _uwt[i].count = (uint16_t)(_uwt[i].count + count);
      return true;
    }
  }
  if (_uwtN >= CF_UWT_MAX) return false;
  _uwt[_uwtN].used = 1;
  _uwt[_uwtN].startLba = startLba;
  _uwt[_uwtN].count = (uint16_t)count;
  _uwt[_uwtN].val = val;
  ++_uwtN;
  return true;
}

// A real (non-uniform) write of [s, s+n) takes precedence over recorded
// content: every overlapping run is carved so only the parts outside the
// real write stay virtual. A carve that cannot be recorded (table full) is
// persisted through uwtWriteOut instead - correct, just slower. Appended
// remainders never overlap [s, s+n), so growing _uwtN mid-scan is safe.
void ZxSpectrumCf::uwtInvalidate(uint32_t s, uint32_t n) {
  const uint32_t e = s + n;
  for (uint16_t i = 0; i < _uwtN; ++i) {
    if (!_uwt[i].used) continue;
    const uint32_t es = _uwt[i].startLba;
    const uint32_t ee = es + _uwt[i].count;
    if (ee <= s || es >= e) continue;
    const uint8_t v = _uwt[i].val;
    const uint32_t leftLen = (s > es) ? (s - es) : 0;
    const uint32_t rightLen = (ee > e) ? (ee - e) : 0;
    _uwt[i].used = 0;
    bool ok = true;
    if (leftLen > 0) ok = uwtAppend(es, leftLen, v);
    if (ok && rightLen > 0) ok = uwtAppend(e, rightLen, v);
    if (!ok) {
      // Table full: what could not be kept virtual must exist in the file,
      // or reads of it would return whatever the preallocated area holds.
      if (leftLen > 0) uwtWriteOut(es, leftLen, v);
      if (rightLen > 0) uwtWriteOut(e, rightLen, v);
    }
  }
}

// Resolve one read LBA: the fabricated byte value of the covering run, or
// -1 = miss (read the real file / fabricate 0xFF beyond the image). Linear
// scan over <= 1100 entries, a few hundred ns per sector.
int ZxSpectrumCf::uwtLookup(uint32_t lba) const {
  for (uint16_t i = 0; i < _uwtN; ++i) {
    if (_uwt[i].used && lba >= _uwt[i].startLba &&
        lba - _uwt[i].startLba < _uwt[i].count) {
      return _uwt[i].val;
    }
  }
  return -1;
}

// Persist a uniform run to the file (fallback when the table cannot hold
// it): 4K chunks of the pattern, one f_lseek+f_write per chunk. Called only
// from the rare table-full split path, with _buf free of transfer data.
void ZxSpectrumCf::uwtWriteOut(uint32_t startLba, uint32_t count, uint8_t val) {
  memset(_buf, val, sizeof(_buf));
  uint32_t lba = startLba;
  while (count > 0) {
    const uint16_t n = (count > CF_BUF_SECTS) ? (uint16_t)CF_BUF_SECTS : (uint16_t)count;
    const UINT bytes = (UINT)n * 512;
    if (f_lseek(&_fil, (FSIZE_t)lba * 512) != FR_OK) { fail(); return; }
    UINT bw = 0;
    if (f_write(&_fil, _buf, bytes, &bw) != FR_OK || bw != bytes) { fail(); return; }
    _cmdDirty = true;
    lba += n;
    count -= n;
  }
}
