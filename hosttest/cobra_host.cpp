// Host test harness: runs the real ZxSpectrum/CobraFloppy code of the firmware
// on the Mac and saves screenshots.  Commands (one per argument):
//   disk:<drive>:<path>   mount an image in drive 0..3
//   frames:<n>            run n video frames (50Hz)
//   press:<k>[+<k>...]    hold keys (CS SS CTRL ENTER SPACE F1-F4 ESC A-Z 0-9)
//   release               release all keys
//   type:<text>           type text, '~' = ENTER
//   shot:<file.ppm>       save the screen
//   config:<0|1|2>        jumpers 80K+80col / 80K / 64K
//   reset                 Reset CoBra 1 (NPOR)
//   state                 print CPU / configuration state
#define private public
#define protected public
#include "ZxSpectrum.h"
#undef private
#undef protected
#include <string>
#include <vector>
#include <ctype.h>

static void a_init() {}
static uint32_t a_freq() { return 44100; }
static bool a_ready() { return true; }
static void a_handler(uint32_t, uint32_t, uint32_t, int32_t, uint32_t, bool) {}
zx_spectrum_audio_driver_t _zx_spectrum_audio_drivers[8] = {
  {a_init, a_freq, a_ready, a_handler, "null"}, {a_init, a_freq, a_ready, a_handler, "null"},
  {a_init, a_freq, a_ready, a_handler, "null"}, {a_init, a_freq, a_ready, a_handler, "null"},
  {a_init, a_freq, a_ready, a_handler, "null"}, {a_init, a_freq, a_ready, a_handler, "null"},
  {a_init, a_freq, a_ready, a_handler, "null"}, {a_init, a_freq, a_ready, a_handler, "null"},
};

class FileMedia : public CobraDiskMedia {
  FILE *_f;
  bool _w;
public:
  FileMedia(const char *path) {
    _f = fopen(path, "r+b");
    _w = _f != 0;
    if (!_f) _f = fopen(path, "rb");
  }
  bool ok() { return _f != 0; }
  int32_t readAt(uint32_t pos, uint8_t *buf, uint32_t len) {
    fseek(_f, pos, SEEK_SET);
    return (int32_t)fread(buf, 1, len, _f);
  }
  int32_t writeAt(uint32_t pos, const uint8_t *buf, uint32_t len) {
    fseek(_f, pos, SEEK_SET);
    int32_t r = (int32_t)fwrite(buf, 1, len, _f);
    fflush(_f);
    return r;
  }
  uint32_t size() { long p = ftell(_f); fseek(_f, 0, SEEK_END); long s = ftell(_f); fseek(_f, p, SEEK_SET); return (uint32_t)s; }
  bool writable() { return _w; }
};

class HostInputStream : public InputStream {
  FILE *_f;
  bool _eof;
public:
  HostInputStream(const char *path) : _f(fopen(path, "rb")), _eof(false) {}
  bool ok() { return _f != 0; }
  int32_t readByte() { int c = fgetc(_f); if (c < 0) { _eof = true; return -1; } return c; }
  int32_t read(uint8_t *b, const uint32_t l) { size_t r = fread(b, 1, l, _f); if (!r) { _eof = true; return -1; } return (int32_t)r; }
  bool closed() { return _f == 0; }
  bool end() { return _eof; }
  int32_t seek(const uint32_t pos) { _eof = false; return fseek(_f, pos, SEEK_SET) ? -2 : 0; }
  int32_t rseek(const int32_t rpos) { _eof = false; return fseek(_f, rpos, SEEK_CUR) ? -2 : 0; }
  uint32_t pos() { return (uint32_t)ftell(_f); }
};

static const uint8_t PAL[16][3] = {
  {0,0,0},{0,0,192},{192,0,0},{192,0,192},{0,192,0},{0,192,192},{192,192,0},{192,192,192},
  {0,0,0},{0,0,255},{255,0,0},{255,0,255},{0,255,0},{0,255,255},{255,255,0},{255,255,255}};

static void cell(uint8_t *img, int W, int x, int y, uint8_t p, uint8_t a) {
  const uint8_t ink = (a & 7) | ((a >> 3) & 8), paper = ((a >> 3) & 7) | ((a >> 3) & 8);
  for (int b = 0; b < 8; ++b) {
    const uint8_t *c = PAL[(p & (0x80 >> b)) ? ink : paper];
    memcpy(&img[(y * W + x + b) * 3], c, 3);
  }
}

static void shot(ZxSpectrum &zx, const char *file) {
  const int W = 320, H = 240;
  std::vector<uint8_t> img(W * H * 3);
  const uint8_t *bc = PAL[zx.borderColour() & 7];
  for (int i = 0; i < W * H; ++i) memcpy(&img[i * 3], bc, 3);
  const bool c80 = zx.cobra80Char();
  uint8_t *s = c80 ? zx.cobraVram() : zx.screenPtr();
  for (int v = 0; v < 192; ++v) {
    const int line = ((v & 7) << 8) + ((v & 0x38) << 2) + ((v & 0xc0) << 5);
    const int attr = 0x1800 + ((v >> 3) << 5);
    for (int i = 0; i < 32; ++i) cell(img.data(), W, 32 + i * 8, 24 + v, s[line + i], s[attr + i]);
    if (c80) {
      const uint8_t *hi = zx.cobraVram() + 0x2000;
      for (int i = 0; i < 4; ++i) cell(img.data(), W, i * 8, 24 + v, hi[line + 28 + i], hi[attr + 28 + i]);
      for (int i = 0; i < 4; ++i) cell(img.data(), W, 288 + i * 8, 24 + v, hi[line + i], hi[attr + i]);
    }
  }
  FILE *f = fopen(file, "wb");
  fprintf(f, "P6 %d %d 255\n", W, H);
  fwrite(img.data(), 1, img.size(), f);
  fclose(f);
}

static ZxSpectrumKeyboard kb(0, 0);

static bool keyFor(const std::string &k, uint8_t &line, uint8_t &mask) {
  static const char *rows[8] = {"#ZXCV", "ASDFG", "QWERT", "12345", "09876", "POIUY", "@LKJH", " $MNB"};
  if (k == "CS") { line = 0; mask = 1; return true; }
  if (k == "SS") { line = 7; mask = 2; return true; }
  if (k == "ENTER") { line = 6; mask = 1; return true; }
  if (k == "SPACE") { line = 7; mask = 1; return true; }
  if (k == "CTRL") { line = 0; mask = 0x20; return true; }
  if (k == "NOSCR") { line = 1; mask = 0x20; return true; }
  if (k == "LF") { line = 2; mask = 0x20; return true; }
  if (k == "ESC") { line = 3; mask = 0x20; return true; }
  if (k.size() == 2 && k[0] == 'F' && k[1] >= '1' && k[1] <= '4') { line = 4 + k[1] - '1'; mask = 0x20; return true; }
  if (k.size() == 1) {
    const char c = toupper(k[0]);
    for (int l = 0; l < 8; ++l)
      for (int b = 0; b < 5; ++b)
        if (rows[l][b] == c && c != '#' && c != '@' && c != '$') { line = l; mask = 1 << b; return true; }
  }
  return false;
}

static void frames(ZxSpectrum &zx, int n) {
  for (int i = 0; i < n; ++i) {
    uint32_t t = 0;
    while (t < 311 * 228) t += zx.step();
  }
}

static void press(const std::string &spec) {
  size_t p = 0;
  while (p <= spec.size()) {
    size_t q = spec.find('+', p);
    if (q == std::string::npos) q = spec.size();
    uint8_t l, m;
    const std::string k = spec.substr(p, q - p);
    if (keyFor(k, l, m)) kb.press(l, m);
    else fprintf(stderr, "unknown key %s\n", k.c_str());
    p = q + 1;
  }
}

int main(int argc, char **argv) {
  static ZxSpectrum zx(&kb, 0, 0, 0);
  zx.setAudioDriver(&_zx_spectrum_audio_drivers[0]);
  zx.reset(ZxSpectrumCobra);
  std::vector<FileMedia *> media;
  for (int a = 1; a < argc; ++a) {
    std::string c = argv[a];
    if (c.rfind("disk:", 0) == 0) {
      const int d = c[5] - '0';
      FileMedia *m = new FileMedia(c.c_str() + 7);
      media.push_back(m);
      printf("disk %d: %s %s\n", d, c.c_str() + 7, m->ok() && zx.cobraInsertDisk(d, m) ? "ok" : "FAILED");
    }
    else if (c.rfind("frames:", 0) == 0) frames(zx, atoi(c.c_str() + 7));
    else if (c.rfind("press:", 0) == 0) press(c.substr(6));
    else if (c == "release") kb.reset();
    else if (c.rfind("type:", 0) == 0) {
      for (char ch : c.substr(5)) {
        if (ch == '~') press("ENTER");
        else if (ch == '"') press("SS+P");
        else press(std::string(1, ch));
        frames(zx, 4);
        kb.reset();
        frames(zx, 4);
      }
    }
    else if (c.rfind("shot:", 0) == 0) { shot(zx, c.c_str() + 5); printf("shot %s\n", c.c_str() + 5); }
    else if (c == "machine:tims") zx.reset(ZxSpectrumTimsPlus);
    else if (c == "machine:cubz") zx.reset(ZxSpectrumCubz);
    else if (c.rfind("cdisk:", 0) == 0) {
      FileMedia *m = new FileMedia(c.c_str() + 8);
      printf("cubz disk %c: %s %s\n", c[6], c.c_str() + 8, m->ok() && zx.cubzInsertDisk(c[6] - '0', m) ? "ok" : "FAILED");
    }
    else if (c.rfind("ckey:", 0) == 0) {
      for (char ch : c.substr(5)) { zx.cubzKey(ch == '~' ? 13 : (uint8_t)ch); frames(zx, 3); }
    }
    else if (c.rfind("cshot:", 0) == 0) {
      FILE *f = fopen(c.c_str() + 6, "wb");
      fprintf(f, "P5 512 256 255\n");
      const uint8_t *v = zx.cubzVram();
      for (int y = 0; y < 256; ++y) {
        const int sy = (y + zx.cubzScroll()) & 255;
        for (int col = 0; col < 64; ++col) {
          const uint8_t b = v[col * 256 + sy];
          for (int k = 0; k < 8; ++k) fputc((b & (0x80 >> k)) ? 255 : 0, f);
        }
      }
      fclose(f);
      printf("cshot %s scroll=%d\n", c.c_str() + 6, zx.cubzScroll());
    }
    else if (c == "cstate") {
      printf("PC=%04X SP=%04X pioB=%02X rom=%d dma=%d addr=%04X cnt=%04X\n", Z80_PC(zx._Z80), Z80_SP(zx._Z80),
        zx._cubzPioB, zx._cubzRom, zx._cubzDmaActive, zx._cubzDmaAddr, zx._cubzDmaCount);
    }
    else if (c == "machine:48") zx.reset(ZxSpectrum48k);
    else if (c.rfind("tdisk:", 0) == 0) {
      HostInputStream *is = new HostInputStream(c.c_str() + 8);
      printf("tims disk %c: %s %s\n", c[6], c.c_str() + 8, is->ok() && zx.hcFdc().mount(c[6] - '0', is) ? "ok" : "FAILED");
    }
    else if (c == "tstate") {
      printf("PC=%04X SP=%04X IM=%d IFF1=%d paging=%02X sec=%02X pc=%02X pa=%02X pb=%02X ctrl=%02X pages=%d,%d,%d,%d\n",
        Z80_PC(zx._Z80), Z80_SP(zx._Z80), zx._Z80.im, zx._Z80.iff1, zx._timsPaging, zx._timsSecondary, zx._timsPc,
        zx._timsPa, zx._timsPb, zx._timsControl, zx._timsPages[0], zx._timsPages[1], zx._timsPages[2], zx._timsPages[3]);
    }
    else if (c.rfind("config:", 0) == 0) zx.cobraConfig((CobraConfig)atoi(c.c_str() + 7));
    else if (c == "reset") zx.reset(ZxSpectrumCobra);
    else if (c == "state") {
      printf("PC=%04X SP=%04X IM=%d IFF1=%d PO=%d LO6=%d portC=%02X ctrl=%02X 80col=%d av13=%d\n",
        Z80_PC(zx._Z80), Z80_SP(zx._Z80), zx._Z80.im, zx._Z80.iff1, zx._cobraPO, zx._cobraLO6,
        zx.cobraPortC(), zx._cobraPpiCtrl, zx._cobra80, zx.cobraAv13());
    }
    else if (c.rfind("dump:", 0) == 0) {
      // dump:<bank>:<file>  raw 16K RAM bank
      FILE *f = fopen(c.c_str() + 7, "wb");
      fwrite(zx._RAM[c[5] - '0'], 1, 0x4000, f);
      fclose(f);
    }
    else if (c.rfind("tvram:", 0) == 0) {
      FILE *f = fopen(c.c_str() + 6, "wb"); fwrite(zx._timsVram, 1, 0x4000, f); fclose(f);
      int nz = 0; for (int i = 0; i < 0x4000; ++i) nz += zx._timsVram[i] != 0;
      printf("tims vram nonzero=%d\n", nz);
    }
    else if (c.rfind("tram:", 0) == 0) {
      // tram:<page hex>:<file>
      int pg = strtol(c.substr(5, 1).c_str(), 0, 16);
      FILE *f = fopen(c.c_str() + 7, "wb"); fwrite(zx._timsRAM[pg], 1, 0x4000, f); fclose(f);
    }
    else if (c.rfind("mem:", 0) == 0) {
      unsigned a = strtoul(c.c_str() + 4, 0, 16);
      printf("%04X:", a);
      for (int i = 0; i < 16; ++i) printf(" %02X", zx.readByte((uint16_t)(a + i)));
      printf("\n");
    }
    else fprintf(stderr, "unknown command %s\n", c.c_str());
  }
  return 0;
}
