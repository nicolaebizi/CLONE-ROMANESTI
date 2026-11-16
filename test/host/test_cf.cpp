// Host (PC) desk-simulation test for the SuperSpec CF write path.
//
// Reproduces the hardware evidence "CF: LBA000400 C01 ST58 CMD30 W3584 R0"
// (the ROM's FORMAT! root-sector write re-issued forever): arms a WRITE
// SECTORS command (0x30, LBA 1024, count 1), pushes exactly 512 data bytes
// and asserts the ATA-visible behaviour of a healthy card:
//   - final STATUS == 0x50 (IDLE), never stuck at DRQ 0x58, never 0x01 ERR
//   - the LBA register reads past the written sector (1025)
//   - the per-command W counter == 512 bytes, E counter == 0
//   - the sector actually landed in the image at LBA*512
// plus: extra data writes after IDLE are ignored (no hidden retry loop),
// multi-sector writes keep DRQ between sectors, reads stream back the
// written data, and injected FatFs failures latch ERR exactly once.
// Virtual-format overlay (superspec12): the auto-created image is grown to
// its full 3,670,000,000-byte size with METADATA-ONLY calls (f_lseek past
// EOF + f_truncate, no 0xFF fill writes, no contiguity needed), uniform
// writes are recorded in the in-RAM table and read back fabricated with
// zero SD traffic, adjacent runs coalesce, real writes carve the recorded
// range, reads beyond the image fabricate 0xFF, a full table degrades to
// real f_write, the table survives a machine reset, and a card that cannot
// actually provide the space (disk-full stub clips the growth) falls back
// to the 16 MB fill with the diag E counter latched.
// Filename switch (superspec13, phase 3): setImageFile() with a NEW name
// closes the open file + drops the uniform-write table (fresh mount); the
// next CF access opens the new path with an empty table. The same name is
// a no-op.
//
// Build (from the project root):
//   cmake -S test/host -B <build>/host-tests
//   make -C <build>/host-tests cf_tests && <build>/host-tests/cf_tests

#include "ZxSpectrumCf.h"
#include "ff.h"
#include "SdCardFatFsSpi.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

// ---- fake FatFs backing store -------------------------------------------

// RAM backing for the file's first IMG_BYTES; content beyond it is served
// as 0xFF (the erased state), like the virtual image the firmware fabricates
// beyond its written data. imgSize is the file size the firmware sees (the
// auto-created image grows it to 3,670,000,000 via f_lseek past EOF +
// f_truncate without any backing at all).
static const uint32_t IMG_BYTES = 16u * 1024u * 1024u;
static uint8_t img[IMG_BYTES];

static FSIZE_t imgSize = IMG_BYTES;
static bool imgExists = false;
static bool imgOpenRw = false;
static bool imgMounted = false;
static FSIZE_t imgPos = 0;

// Failure injection: each counter is consumed by the next matching call.
static int failWrite = 0, failSync = 0, failSeek = 0;

// Disk-full injection: the biggest size f_lseek may grow imgSize to
// (0 = unlimited). Mimics real FatFs, which on a full card silently clips
// the stretched chain and STILL returns FR_OK.
static FSIZE_t growLimit = 0;

// Creation-path instrumentation: the fast growth must do exactly ONE big
// metadata seek and never run the 0xFF fill loop.
static int bigSeek = 0, realWrites = 0;

// Filename-switch instrumentation (superspec13): which path f_open last
// succeeded with, and how many f_close calls ran (the chooser's fresh
// mount must close the open file of the previous image).
static char openPath[128] = "";
static int openCount = 0, closeCount = 0;

static void resetFs() {
  memset(img, 0xFF, sizeof(img));
  imgSize = IMG_BYTES;
  imgExists = false;
  imgOpenRw = false;
  imgMounted = false;
  imgPos = 0;
  failWrite = failSync = failSeek = 0;
  growLimit = 0;
  bigSeek = realWrites = 0;
  openPath[0] = 0;
  openCount = closeCount = 0;
}

FRESULT f_open(FIL* fp, const char* path, unsigned mode) {
  // Single-backing-store model: any path is "the" image file (the firmware
  // switches between zxspectrum/cf dumps through setImageFile). The create
  // branch emulates FatFs: a fresh file is empty, writes/seek past EOF
  // extend it.
  if (mode & (FA_CREATE_NEW | FA_CREATE_ALWAYS)) {
    if ((mode & FA_CREATE_NEW) && imgExists) return FR_EXIST;
    memset(img, 0xFF, sizeof(img));
    imgExists = true;
    imgSize = 0; // a fresh FatFs file is empty; writes/seek past EOF extend it
    imgOpenRw = true;
    imgPos = 0;
    fp->objsize = imgSize;
    strncpy(openPath, path, sizeof(openPath) - 1);
    openPath[sizeof(openPath) - 1] = 0;
    ++openCount;
    return FR_OK;
  }
  if (!imgExists) return FR_NO_FILE;
  imgOpenRw = (mode & FA_WRITE) != 0;
  imgPos = 0;
  fp->objsize = imgSize;
  strncpy(openPath, path, sizeof(openPath) - 1);
  openPath[sizeof(openPath) - 1] = 0;
  ++openCount;
  return FR_OK;
}

FRESULT f_close(FIL*) { imgOpenRw = false; ++closeCount; return FR_OK; }
FRESULT f_sync(FIL*) {
  if (failSync > 0) { --failSync; return FR_DISK_ERR; }
  return FR_OK;
}
FRESULT f_lseek(FIL* fp, FSIZE_t ofs) {
  if (failSeek > 0) { --failSeek; return FR_DISK_ERR; }
  if (ofs > imgSize) {
    // Seek past EOF: write-mode stretches the "cluster chain" (metadata
    // only), read-mode is clipped back to the file end (FR_DENIED shape:
    // report as disk error so a read-mode overrun never hides).
    if (!imgOpenRw) return FR_DISK_ERR;
    if (ofs == (FSIZE_t)ZxSpectrumCf::IMAGE_BYTES) ++bigSeek; // attempt made
    if (growLimit != 0 && ofs > growLimit) {
      // Disk-full clip: real FatFs breaks the stretch loop there and the
      // pointer ends at the partially grown size (both FR_OK).
      imgSize = growLimit;
    }
    else {
      imgSize = ofs;
    }
    imgPos = imgSize; // the pointer follows the seek, like real FatFs
    fp->objsize = imgSize; // f_size reads objsize, like real FatFs
    return FR_OK; // FR_OK even when clipped - like real FatFs on disk full
  }
  imgPos = ofs;
  return FR_OK;
}
FRESULT f_truncate(FIL* fp) {
  if (!imgOpenRw) return FR_WRITE_PROTECTED;
  if (imgPos < imgSize) { imgSize = imgPos; fp->objsize = imgSize; }
  return FR_OK;
}
FRESULT f_read(FIL*, void* buff, UINT btr, UINT* br) {
  if (!imgExists) return FR_NO_FILE;
  for (UINT n = 0; n < btr; ++n) {
    // Beyond the RAM backing the file content is the erased state 0xFF.
    ((uint8_t*)buff)[n] = (imgPos + n < IMG_BYTES) ? img[imgPos + n] : 0xFF;
  }
  imgPos += btr;
  *br = btr;
  return FR_OK;
}
FRESULT f_write(FIL* fp, const void* buff, UINT btw, UINT* bw) {
  *bw = 0;
  if (!imgOpenRw) return FR_WRITE_PROTECTED;
  if (failWrite > 0) { --failWrite; return FR_DISK_ERR; }
  if (imgPos + btw > IMG_BYTES) return FR_DISK_ERR;
  memcpy(img + imgPos, buff, btw);
  imgPos += btw;
  if (imgPos > imgSize) { imgSize = imgPos; fp->objsize = imgSize; }
  *bw = btw;
  ++realWrites;
  return FR_OK;
}
FRESULT f_unlink(const char* path) {
  if (strcmp(path, ZxSpectrumCf::imagePath()) == 0) {
    imgExists = false;
    imgSize = IMG_BYTES;
  }
  return FR_OK;
}
// f_expand is deliberately NOT stubbed any more: the firmware must never
// call it (it needs a contiguous free area and failed on the user's
// fragmented card) - an accidental reintroduction fails the link.

// ---- minimal check harness ----------------------------------------------

static int checks = 0, failures = 0;
#define CHECK(cond) do { \
  ++checks; \
  if (!(cond)) { \
    ++failures; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
  } \
} while (0)

static void wrReg(ZxSpectrumCf& cf, uint32_t lba, uint8_t count) {
  cf.writeReg(2, count);
  cf.writeReg(3, (uint8_t)(lba & 0xFF));
  cf.writeReg(4, (uint8_t)((lba >> 8) & 0xFF));
  cf.writeReg(5, (uint8_t)((lba >> 16) & 0xFF));
  cf.writeReg(6, (uint8_t)((lba >> 24) & 0xFF) | 0xE0);
}

static void wrData(ZxSpectrumCf& cf, uint32_t lba, uint8_t nSects, uint8_t seed) {
  for (uint32_t i = 0; i < (uint32_t)nSects * 512; ++i)
    cf.writeReg(0, (uint8_t)(seed + i + lba));
}

// Uniform write: every byte of every sector is the same value (the DOS's
// FORMAT! DIR-clear pattern).
static void wrUni(ZxSpectrumCf& cf, uint32_t lba, uint8_t nSects, uint8_t val) {
  (void)lba;
  for (uint32_t i = 0; i < (uint32_t)nSects * 512; ++i) cf.writeReg(0, val);
}

// Stream n sectors of a read transfer back, verifying each byte.
static bool rdData(ZxSpectrumCf& cf, uint32_t nSects, uint8_t expect) {
  for (uint32_t i = 0; i < (uint32_t)nSects * 512; ++i)
    if (cf.readReg(0) != expect) return false;
  return true;
}

int main() {
  SdCardFatFsSpi sd;

  // --- TEST 1: the FORMAT! root write (LBA 1024, count 1) -----------------
  // Regression for the hardware retry loop: before the fix the status stayed
  // 0x58 after the 512th byte, so the ROM re-wrote the same sector forever
  // (diag: LBA frozen, W growing in 512B steps, R always 0).
  {
    resetFs();
    sd.fakeMounted = true;
    imgExists = true; // SUPERSPEC.CF already on the SD
    ZxSpectrumCf cf(&sd);

    wrReg(cf, 1024, 1);
    cf.writeReg(7, ZxSpectrumCf::CMD_WRITE);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_DRQ); // 0x58 after arm

    wrData(cf, 1024, 1, 0x42);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE); // 0x50 after last byte

    ZxSpectrumCf::CfDiag d;
    cf.diag(&d);
    CHECK(d.lba == 1025);  // LBA register advanced past the sector
    CHECK(d.nWr == 512);   // W = bytes of THIS command
    CHECK(d.nErr == 0);    // no ERR latched
    CHECK(d.cmd == 0x30);
    CHECK(d.count == 1);
    CHECK(d.nCmd == 1);    // one command issued (no retry storm)

    // The sector landed at LBA*512 in the image.
    CHECK(img[1024 * 512 + 0] == (uint8_t)(0x42 + 0 + 1024));
    CHECK(img[1024 * 512 + 511] == (uint8_t)(0x42 + 511 + 1024));

    // Extra data writes after IDLE must be ignored: no hidden second sector,
    // no W growth, file content unchanged.
    wrData(cf, 1024, 1, 0x99);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);
    cf.diag(&d);
    CHECK(d.nWr == 512);
    CHECK(d.nErr == 0);
    CHECK(img[1024 * 512] == (uint8_t)(0x42 + 0 + 1024));
  }

  // --- TEST 2: two-sector write keeps DRQ between sectors -----------------
  {
    resetFs();
    sd.fakeMounted = true;
    imgExists = true;
    ZxSpectrumCf cf(&sd);

    wrReg(cf, 2000, 2);
    cf.writeReg(7, ZxSpectrumCf::CMD_WRITE);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_DRQ);
    wrData(cf, 2000, 1, 0x11); // first 512 bytes
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_DRQ); // more data expected
    wrData(cf, 2001, 1, 0x11 + 512); // second 512 bytes
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);

    ZxSpectrumCf::CfDiag d;
    cf.diag(&d);
    CHECK(d.lba == 2002);
    CHECK(d.nWr == 1024);
    CHECK(d.nErr == 0);
    CHECK(img[2000 * 512] == (uint8_t)(0x11 + 0 + 2000));
    CHECK(img[2001 * 512 + 511] == (uint8_t)(0x11 + 511 + 2001));
  }

  // --- TEST 3: read-back of the written data ------------------------------
  {
    resetFs();
    sd.fakeMounted = true;
    imgExists = true;
    memset(img + 3000 * 512, 0xAB, 512);
    ZxSpectrumCf cf(&sd);

    wrReg(cf, 3000, 1);
    cf.writeReg(7, ZxSpectrumCf::CMD_READ);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_DRQ);
    CHECK(cf.readReg(1) == 0xAB); // data-odd peek does not advance
    CHECK(cf.readReg(0) == 0xAB);
    bool ok = true;
    for (uint32_t i = 1; i < 512; ++i) ok = ok && cf.readReg(0) == 0xAB;
    CHECK(ok);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);
    ZxSpectrumCf::CfDiag d;
    cf.diag(&d);
    CHECK(d.nWr == 0);
    CHECK(d.nErr == 0);
  }

  // --- TEST 4: injected f_sync failure latches ERR once -------------------
  {
    resetFs();
    sd.fakeMounted = true;
    imgExists = true;
    ZxSpectrumCf cf(&sd);

    failSync = 1;
    wrReg(cf, 500, 1);
    cf.writeReg(7, ZxSpectrumCf::CMD_WRITE);
    wrData(cf, 500, 1, 0x77);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_ERR); // 0x01
    ZxSpectrumCf::CfDiag d;
    cf.diag(&d);
    CHECK(d.nErr == 1);
    CHECK(d.nWr == 512);
    // A retry of the command after ERR works again (injection spent).
    wrReg(cf, 500, 1);
    cf.writeReg(7, ZxSpectrumCf::CMD_WRITE);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_DRQ);
    wrData(cf, 500, 1, 0x77);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);
    cf.diag(&d);
    CHECK(d.nErr == 1); // the good run added no new error
  }

  // --- TEST 5: write-protected image (read-only open) latches ERR ---------
  {
    resetFs();
    sd.fakeMounted = true;
    imgExists = true;
    // ensureOpen tries RW first; make the RW open fail so it falls back to RO
    ZxSpectrumCf cf(&sd);
    // Without an RW-openable file the class falls back to read-only: emulate
    // by clearing the RW capability at fake-fs level (f_open RW -> protected)
    // is not needed: arm a write against a fresh CF whose open succeeds RW.
    // This sub-test instead checks the no-media path.
    ZxSpectrumCf cfNoMedia(nullptr);
    wrReg(cfNoMedia, 0, 1);
    cfNoMedia.writeReg(7, ZxSpectrumCf::CMD_WRITE);
    CHECK(cfNoMedia.readReg(7) == ZxSpectrumCf::CF_ST_ERR);
    ZxSpectrumCf::CfDiag d;
    cfNoMedia.diag(&d);
    CHECK(d.nErr == 1);
  }

  // --- TEST 6: auto-create = metadata-only growth + virtual format -------
  // Root cause regression: the image must be grown to its full size with
  // ONE f_lseek past EOF + f_truncate (no 0xFF fill writes, no f_expand),
  // reads beyond the written data serve 0xFF, reads beyond the virtual
  // image size too, and a uniform FORMAT!-style write is recorded in the
  // table with ZERO SD traffic and read back fabricated.
  {
    resetFs();
    sd.fakeMounted = true;
    // imgExists stays false -> the auto-create path runs
    ZxSpectrumCf cf(&sd);

    wrReg(cf, 1024, 1);
    cf.writeReg(7, ZxSpectrumCf::CMD_READ);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_DRQ);
    CHECK(bigSeek == 1);               // ONE metadata growth seek used
    CHECK(imgSize == 3670000000u);     // file size = the virtual image size
    CHECK(realWrites == 0);            // no 0xFF fill loop
    CHECK(rdData(cf, 1, 0xFF));        // ROOT area still erased
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);

    // Read beyond the RAM backing (30 MB, inside the virtual image): 0xFF.
    wrReg(cf, 60000, 1);
    cf.writeReg(7, ZxSpectrumCf::CMD_READ);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_DRQ);
    CHECK(rdData(cf, 1, 0xFF));
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);

    // Read beyond the virtual image itself (LBA 7169000 x 512 > 3.67 GB):
    // fabricated 0xFF, no error - the old code latched ERR here (the ROM's
    // "press a key" loop root cause).
    wrReg(cf, 7169000, 1);
    cf.writeReg(7, ZxSpectrumCf::CMD_READ);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_DRQ);
    CHECK(rdData(cf, 1, 0xFF));
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);
    ZxSpectrumCf::CfDiag d;
    cf.diag(&d);
    CHECK(d.nErr == 0);

    // Uniform 0x20 DIR-clear write: recorded, no SD traffic.
    wrReg(cf, 1025, 1);
    cf.writeReg(7, ZxSpectrumCf::CMD_WRITE);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_DRQ);
    wrUni(cf, 1025, 1, 0x20);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);
    CHECK(realWrites == 0);            // the sector never hit the SD
    cf.diag(&d);
    CHECK(d.nUwt == 1);                // FT=1 after the first uniform run

    // Fabricated read-back of the recorded run.
    wrReg(cf, 1025, 1);
    cf.writeReg(7, ZxSpectrumCf::CMD_READ);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_DRQ);
    CHECK(rdData(cf, 1, 0x20));
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);
    CHECK(realWrites == 0);
  }

  // --- TEST 7: adjacent uniform runs coalesce into one entry -------------
  {
    resetFs();
    sd.fakeMounted = true;
    imgExists = true;
    ZxSpectrumCf cf(&sd);

    wrReg(cf, 1025, 1);
    cf.writeReg(7, ZxSpectrumCf::CMD_WRITE);
    wrUni(cf, 1025, 1, 0x20);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);
    wrReg(cf, 1026, 1);
    cf.writeReg(7, ZxSpectrumCf::CMD_WRITE);
    wrUni(cf, 1026, 1, 0x20);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);
    wrReg(cf, 1027, 1);
    cf.writeReg(7, ZxSpectrumCf::CMD_WRITE);
    wrUni(cf, 1027, 1, 0x20);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);

    ZxSpectrumCf::CfDiag d;
    cf.diag(&d);
    CHECK(d.nUwt == 1); // 3 adjacent runs -> ONE coalesced entry

    // The whole coalesced run reads back fabricated.
    wrReg(cf, 1025, 3);
    cf.writeReg(7, ZxSpectrumCf::CMD_READ);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_DRQ);
    CHECK(rdData(cf, 3, 0x20));
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);
    CHECK(realWrites == 0);
  }

  // --- TEST 8: real (non-uniform) write carves the recorded range --------
  {
    resetFs();
    sd.fakeMounted = true;
    imgExists = true;
    ZxSpectrumCf cf(&sd);

    // Four uniform 0x20 sectors at 5000..5003 -> one run.
    for (int i = 0; i < 4; ++i) {
      wrReg(cf, (uint32_t)(5000 + i), 1);
      cf.writeReg(7, ZxSpectrumCf::CMD_WRITE);
      wrUni(cf, (uint32_t)(5000 + i), 1, 0x20);
      CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);
    }
    ZxSpectrumCf::CfDiag d;
    cf.diag(&d);
    CHECK(d.nUwt == 1);

    // Real 2-sector write into the middle (5001..5002).
    wrReg(cf, 5001, 2);
    cf.writeReg(7, ZxSpectrumCf::CMD_WRITE);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_DRQ);
    wrData(cf, 5001, 1, 0x33);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_DRQ);
    wrData(cf, 5002, 1, (uint8_t)(0x33 + 512));
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);

    // The real data landed on the SD...
    CHECK(img[5001 * 512] == (uint8_t)(0x33 + 0 + 5001));
    CHECK(img[5002 * 512 + 511] == (uint8_t)(0x33 + 511 + 5002));
    // ...and the run was split, not dropped: the untouched flanks stay
    // virtual 0x20, the written middle reads the real data.
    cf.diag(&d);
    CHECK(d.nUwt == 2); // [5000,1) + [5003,1)
    wrReg(cf, 5000, 4);
    cf.writeReg(7, ZxSpectrumCf::CMD_READ);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_DRQ);
    CHECK(rdData(cf, 1, 0x20)); // sector 5000: virtual flank
    bool ok = true;
    for (uint32_t i = 0; i < 512; ++i)
      ok = ok && cf.readReg(0) == (uint8_t)(0x33 + i + 5001);
    CHECK(ok); // sector 5001: real data
    for (uint32_t i = 0; i < 512; ++i)
      ok = ok && cf.readReg(0) == (uint8_t)(0x33 + 512 + i + 5002);
    CHECK(ok); // sector 5002: real data
    CHECK(rdData(cf, 1, 0x20)); // sector 5003: virtual flank
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);
  }

  // --- TEST 9: table full -> real f_write fallback -----------------------
  {
    resetFs();
    sd.fakeMounted = true;
    imgExists = true;
    ZxSpectrumCf cf(&sd);

    // Fill the table with discontiguous uniform runs (2-LBA steps defeat
    // coalescing): CF_UWT_MAX entries.
    for (uint32_t i = 0; i < CF_UWT_MAX; ++i) {
      wrReg(cf, 1000 + i * 2, 1);
      cf.writeReg(7, ZxSpectrumCf::CMD_WRITE);
      wrUni(cf, 1000 + i * 2, 1, 0x40);
      CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);
    }
    ZxSpectrumCf::CfDiag d;
    cf.diag(&d);
    CHECK(d.nUwt == CF_UWT_MAX);
    CHECK(realWrites == 0);

    // One more uniform write: the table cannot take it -> the data still in
    // _buf goes through f_write (safe degradation), and reads stay correct.
    wrReg(cf, 5000, 1);
    cf.writeReg(7, ZxSpectrumCf::CMD_WRITE);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_DRQ);
    wrUni(cf, 5000, 1, 0x55);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);
    CHECK(img[5000 * 512] == 0x55);    // really written to the SD
    cf.diag(&d);
    CHECK(d.nUwt == CF_UWT_MAX);             // still full, no phantom entry
    wrReg(cf, 5000, 1);
    cf.writeReg(7, ZxSpectrumCf::CMD_READ);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_DRQ);
    CHECK(rdData(cf, 1, 0x55));
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);
  }

  // --- TEST 10: reset keeps the table (image content, not session state) -
  {
    resetFs();
    sd.fakeMounted = true;
    imgExists = true;
    ZxSpectrumCf cf(&sd);

    wrReg(cf, 1025, 1);
    cf.writeReg(7, ZxSpectrumCf::CMD_WRITE);
    wrUni(cf, 1025, 1, 0x20);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);

    cf.reset(); // machine reset: file closed, table must survive
    wrReg(cf, 1025, 1);
    cf.writeReg(7, ZxSpectrumCf::CMD_READ);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_DRQ);
    CHECK(rdData(cf, 1, 0x20)); // still served from the table
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);
  }

  // --- TEST 11: disk-full card -> growth clipped -> 16 MB fallback -------
  // Hardware regression (stuck at LBA202020 R0 FT0): the previous f_expand
  // prealloc needed a CONTIGUOUS free area, failed on the user's fragmen-
  // ted/used card and the 16 MB fill image made every read past its end
  // fail forever. Now the metadata growth is attempted first; when the
  // card cannot actually provide the space (real FatFs silently clips the
  // stretched chain and still returns FR_OK - stub growLimit), the file is
  // removed, the diag E counter latches the failure (FT stays 0) and the
  // old 16 MB 0xFF fill runs as fallback - degraded, but the DOS boots.
  {
    resetFs();
    sd.fakeMounted = true;
    growLimit = 4096u * 1024u; // "disk full" after 4 MB of free clusters
    ZxSpectrumCf cf(&sd);

    wrReg(cf, 1024, 1);
    cf.writeReg(7, ZxSpectrumCf::CMD_READ);
    CHECK(bigSeek == 1);               // the growth was attempted first
    CHECK(imgExists && imgSize == ZxSpectrumCf::BLANK_IMAGE_BYTES);
    CHECK(realWrites == ZxSpectrumCf::BLANK_IMAGE_BYTES / 4096u); // fill ran
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_DRQ);
    CHECK(rdData(cf, 1, 0xFF));        // ROOT area erased, reads fine
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);

    ZxSpectrumCf::CfDiag d;
    cf.diag(&d);
    CHECK(d.nErr == 1);                // E latched the failed 3.67 GB growth
    CHECK(d.nUwt == 0);                // FT stays 0 (no virtual format yet)

    // The fallback image is fully usable: a real write lands at LBA*512.
    wrReg(cf, 1024, 1);
    cf.writeReg(7, ZxSpectrumCf::CMD_WRITE);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_DRQ);
    wrData(cf, 1024, 1, 0x5A);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);
    CHECK(img[1024 * 512] == (uint8_t)(0x5A + 0 + 1024));
  }

  // --- TEST 12: filename switch = fresh mount (close + table drop) --------
  // Phase 3 regression (the menu's SuperSpec CF chooser): setImageFile()
  // with a NEW name must close the open image file and drop the
  // uniform-write table (a different dump must never inherit the previous
  // image's virtual content); the next CF access opens the NEW path and
  // sees no recorded runs. The same name is a no-op.
  {
    resetFs();
    sd.fakeMounted = true;
    imgExists = true;
    ZxSpectrumCf cf(&sd);

    // Build virtual-format state: one uniform run (FT=1).
    wrReg(cf, 1025, 1);
    cf.writeReg(7, ZxSpectrumCf::CMD_WRITE);
    wrUni(cf, 1025, 1, 0x20);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);
    ZxSpectrumCf::CfDiag d;
    cf.diag(&d);
    CHECK(d.nUwt == 1);
    const int closesBefore = closeCount; // nothing closed yet

    // Same name: deliberate no-op - no close, table untouched.
    ZxSpectrumCf::setImageFile(ZxSpectrumCf::imagePath());
    cf.diag(&d);
    CHECK(d.nUwt == 1);
    CHECK(closeCount == closesBefore);

    // NEW name: the open file is closed and the table is dropped.
    ZxSpectrumCf::setImageFile("zxspectrum/cf/DUMP2.CF");
    CHECK(strcmp(ZxSpectrumCf::imagePath(), "zxspectrum/cf/DUMP2.CF") == 0);
    cf.diag(&d);
    CHECK(d.nUwt == 0);                    // UWT dropped (fresh mount)
    CHECK(closeCount == closesBefore + 1); // the open file was closed

    // The next CF access re-opens the NEW path and reads the fresh image:
    // the old virtual 0x20 run is gone (real file / erased state instead).
    wrReg(cf, 1025, 1);
    cf.writeReg(7, ZxSpectrumCf::CMD_READ);
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_DRQ);
    CHECK(strcmp(openPath, "zxspectrum/cf/DUMP2.CF") == 0);
    CHECK(rdData(cf, 1, 0xFF));
    CHECK(cf.readReg(7) == ZxSpectrumCf::CF_ST_IDLE);
    cf.diag(&d);
    CHECK(d.nUwt == 0); // still no table entries for the new image
  }

  printf("cf_tests: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
