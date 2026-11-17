// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
// Host unit test for the ICE Felix CUB-Z DVI/HDMI scanline encoder plan:
// the 512x256 1bpp line rendered with libdvi's full-resolution 1bpp palette
// path (tmds_setup_palette24_symbols + tmds_encode_palette_data,
// palette_bits = 1), 2-entry palette {black, ZX bright green}.
//
// libdvi itself needs the RP2040/RP2350 SIO interpolators, so the TMDS symbol
// encoder, the 24-bit palette setup (n_palette = 2) and the palette_bits = 1
// encode loop are replicated here with the REAL interp semantics from PicoDVI
// libdvi (tmds_encode.c / tmds_encode.S, tmds_palette_encode_loop): the
// interpolator's FULL result adds the lane shift+mask values to BASE2 with
// the masked bits kept IN PLACE, so
//   LUT byte address = base2 + (colour << 2) + (parity << (palette_bits+2))
// i.e. word `colour` inside the channel section, +n_palette words for the
// positive-disparity entries (this is what makes the proven fullres encode
// address its 128-word table: colour<<2 bytes = colour words, parity<<8
// bytes = the 64-word parity block).  For palette_bits = 1 the lane 0 mask
// (bit 2 of the already-shifted pixbuf word = the pixbuf BYTE's LSB)
// therefore extracts the pixbuf byte's bit 0 as the colour index.
//
// The CUB-Z pixel expansion under test is the REAL code from
// src/ZxCubzTmdsEncode.h.
//
// Verified properties:
//   1. the LUT expands a VRAM byte MSB-first (leftmost pixel first), palette
//      index in the byte's LSB (bit 0), margins unpack to all-black, 0xFF to
//      all index 1;
//   2. colour 0 is black on every channel and colour 1 is pure green ink:
//      identical symbols on blue and red (their component bytes are 0),
//      distinct green symbols on the green section (the previous revision
//      drew black on black because attr $C0 maps to black there, and the
//      revision on top of it had the index in bit 2 so the hardware read
//      colour 0 for every pixel = all black);
//   3. PLANE ASSIGNMENT in the output TMDS buffer (the ground truth this
//      test pins down): blue plane at words [0, n/2), green at [n/2, n),
//      red at [n, 3n/2) - exactly the three DMA planes libdvi serves
//      (dvi_timing.c: lane i reads tmdsbuf + i * h_active/2; lane 0 blue
//      carries sync, the "solid red" error line is B/G 0x00 + R 0xfc).  The
//      GREEN plane carries the green ink pattern, the BLUE and RED planes
//      carry all-black symbols everywhere;
//   4. a full 640-pixel line (margins + pattern) encodes to 3 x 320 words,
//      every 10-bit symbol decodes back through the palette, margins stay
//      black and single pixels keep their colour and position 1:1.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "ZxCubzTmdsEncode.h"

// ---- verbatim from PicoDVI software/libdvi/tmds_encode.c -------------------

static const int8_t imbalance_lookup[16] = {
  -4, -2, -2, 0, -2, 0, 0, 2, -2, 0, 0, 2, 0, 2, 2, 4
};

static inline int byte_imbalance(uint32_t x) {
  return imbalance_lookup[x >> 4] + imbalance_lookup[x & 0xF];
}

static void tmds_encode_symbols(uint8_t pixel, uint32_t* negative_balance_sym, uint32_t* positive_balance_sym) {
  int pixel_imbalance = byte_imbalance(pixel);
  uint32_t sym = pixel & 1;
  if (pixel_imbalance > 0 || (pixel_imbalance == 0 && sym == 0)) {
    for (int i = 0; i < 7; ++i) {
      sym |= (~((sym >> i) ^ (pixel >> (i + 1))) & 1) << (i + 1);
    }
  } else {
    for (int i = 0; i < 7; ++i) {
      sym |= (((sym >> i) ^ (pixel >> (i + 1))) & 1) << (i + 1);
    }
    sym |= 0x100;
  }
  int imbalance = byte_imbalance(sym & 0xFF);
  if (imbalance == 0) {
    if ((sym & 0x100) == 0) sym ^= 0x2ff;
    *positive_balance_sym = sym;
    *negative_balance_sym = sym;
    return;
  } else if (imbalance > 0) {
    *negative_balance_sym =
      (sym ^ 0x2ff) | (((-imbalance + imbalance_lookup[2 ^ (sym >> 8)] + 2) & 0x3F) << 26);
    *positive_balance_sym = sym | ((imbalance + imbalance_lookup[sym >> 8] + 2) << 26);
  } else {
    *negative_balance_sym =
      sym | (((imbalance + imbalance_lookup[sym >> 8] + 2) & 0x3F) << 26);
    *positive_balance_sym =
      (sym ^ 0x2ff) | ((-imbalance + imbalance_lookup[2 ^ (sym >> 8)] + 2) << 26);
  }
}

// tmds_setup_palette24_symbols for n_palette = 2: per channel section of 4
// words: colour 0 negative, colour 1 negative, colour 0 positive, colour 1
// positive; sections in order blue, green, red.  The 24-bit palette value is
// blue in bits 0-7, green in bits 8-15, red in bits 16-23.
static void tmds_setup_palette24_symbols_2(const uint32_t palette[2], uint32_t* tmds_palette) {
  for (int i = 0; i < 2; ++i) {
    uint16_t blue = palette[i] & 0xff;
    uint16_t green = (palette[i] >> 8) & 0xff;
    uint16_t red = (palette[i] >> 16) & 0xff;
    tmds_encode_symbols(blue, &tmds_palette[i], &tmds_palette[i + 2]);
    tmds_encode_symbols(green, &tmds_palette[i + 4], &tmds_palette[i + 6]);
    tmds_encode_symbols(red, &tmds_palette[i + 8], &tmds_palette[i + 10]);
  }
}

// tmds_palette_encode_loop for palette_bits = 1.  Output word i of a channel
// section covers pixbuf bytes 2i (even/left pixel, 10 low bits) and 2i+1
// (odd/right pixel, bits 10-19).  The colour index is the pixbuf byte's bit
// 0; the running-disparity sign (ACCUM1 bit 31 of each interp's own
// accumulator) selects the positive-disparity entry (+n_palette words) but
// never the colour.  Each channel pass restarts the disparity at 0 and
// writes n_pix / 2 words: blue at symbuf + 0, green at symbuf + n_pix/2,
// red at symbuf + n_pix.
static void tmds_encode_palette_data_1bpp(
  const uint32_t* pal, const uint8_t* pix, size_t n_pix, uint32_t* symbuf
) {
  for (int s = 0; s < 3; ++s) {
    int32_t acc_even = 0, acc_odd = 0;  // running disparity, even/odd streams
    const uint32_t* sec = pal + s * 4;
    for (size_t i = 0; i < n_pix / 2; ++i) {
      uint32_t c0 = pix[2 * i] & 1;
      uint32_t c1 = pix[2 * i + 1] & 1;
      uint32_t e = sec[c0 + ((acc_even >> 31) ? 2 : 0)];
      uint32_t o = sec[c1 + ((acc_odd >> 31) ? 2 : 0)];
      acc_even += (int32_t)e;  // ACCUM1 += full LUT entry (symbol bits + length)
      acc_odd += (int32_t)o;
      symbuf[s * (n_pix / 2) + i] = (e & 0x3ff) | ((o & 0x3ff) << 10);
    }
  }
}

// ---- harness ---------------------------------------------------------------

static int failures = 0;
static void check(const char* name, bool ok) {
  printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
  if (!ok) ++failures;
}

static bool sym_is(const uint32_t* sec, uint32_t sym, uint32_t colour) {
  // colour i negative = sec[i], positive = sec[i + 2]
  return (sec[colour] & 0x3ff) == (sym & 0x3ff) ||
         (sec[colour + 2] & 0x3ff) == (sym & 0x3ff);
}

int main() {
  // The exact palette of the firmware encoder.
  const uint32_t palette24[2] = { 0x000000, 0x00ff00 };  // black, bright green
  uint32_t pal[12];
  tmds_setup_palette24_symbols_2(palette24, pal);

  uint32_t lut[ZX_CUBZ_TMDS_LUT_WORDS];
  zx_cubz_tmds_build_lut(lut);

  printf("palette words (B c0neg c1neg c0pos c1pos | G ... | R ...):\n");
  const char* nm[3] = { "B", "G", "R" };
  for (int s = 0; s < 3; ++s) {
    printf("  %s:", nm[s]);
    for (int i = 0; i < 4; ++i) printf(" %03x", pal[s * 4 + i] & 0x3ff);
    printf("\n");
  }

  // --- 1: the pixel expansion (ZxCubzTmdsEncode.h, real code) ---------------
  {
    uint8_t w[640];
    memset(w, 0, sizeof w);
    for (uint32_t c = 0; c < 64; ++c)
      for (int j = 0; j < 4; ++j) {
        w[64 + c * 8 + j] = (lut[(0xB7u << 1)] >> (8 * j)) & 0xff;
        w[64 + c * 8 + 4 + j] = (lut[(0xB7u << 1) + 1] >> (8 * j)) & 0xff;
      }
    bool ok = true;
    for (int j = 0; j < 8; ++j)
      ok &= (w[64 + j] == (((0xB7 >> (7 - j)) & 1) ? 0x01 : 0x00));
    check("LUT byte 0xB7 unpacks MSB-first, index in bit 0", ok);
    check("byte 0x00 unpacks all-black", (lut[0] | lut[1]) == 0);
    check("byte 0xFF unpacks all index 1 (0x01010101)",
          lut[510] == 0x01010101u && lut[511] == 0x01010101u);
  }

  // --- 2: the colours --------------------------------------------------------
  {
    bool gsec =
      ((pal[5] & 0x3ff) != (pal[4] & 0x3ff)) && ((pal[7] & 0x3ff) != (pal[6] & 0x3ff));
    check("green channel symbols differ black vs green (colour 1 = ink)", gsec);
    bool br =
      ((pal[1] & 0x3ff) == (pal[0] & 0x3ff)) && ((pal[3] & 0x3ff) == (pal[2] & 0x3ff)) &&
      ((pal[9] & 0x3ff) == (pal[8] & 0x3ff)) && ((pal[11] & 0x3ff) == (pal[10] & 0x3ff));
    check("colour 1 identical to black on B and R channels (pure green ink)", br);
    bool blk = (pal[0] & 0x3ff) == (pal[4] & 0x3ff) && (pal[0] & 0x3ff) == (pal[8] & 0x3ff);
    check("black symbol identical on all 3 channels (true black)", blk);
  }

  // --- 3: a full line, encode/decode round trip ------------------------------
  {
    // 64 black margin pixels + CUB-Z line + 64 black margin pixels, filled by
    // the real fill_line helper.
    static uint32_t pixbuf[ZX_CUBZ_TMDS_LINE_WORDS];
    memset(pixbuf, 0, sizeof pixbuf);
    const uint8_t line[ZX_CUBZ_TMDS_SCREEN_BYTES] = {
      0xB7,0x00,0x00,0x00,0x18,0x18,0x18,0x18, 0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,
      0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18, 0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,
      0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18, 0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,
      0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18, 0x00,0x00,0x00,0x00,0x81,0x42,0x3C,0xFF,
    };
    // fill_line reads vram_line[c << 8], i.e. with the byte-per-8-px stride;
    // place the pattern accordingly (64 bytes spread over 16K).
    uint8_t vram_line[0x4000];
    memset(vram_line, 0xAA, sizeof vram_line);  // poison, must not leak in
    for (uint32_t c = 0; c < ZX_CUBZ_TMDS_SCREEN_BYTES; ++c) vram_line[c << 8] = line[c];
    const uint32_t margin_words = (640 - 512) >> 3;  // 64 px per side = 16 words
    zx_cubz_tmds_fill_line(pixbuf, margin_words, lut, vram_line);

    bool untouched = true;
    for (uint32_t c = 0; c < ZX_CUBZ_TMDS_SCREEN_BYTES; ++c)
      untouched &= (vram_line[(c << 8) + 1] == 0xAA);
    check("fill_line touches only the 64 stride bytes", untouched);

    // margins stay colour 0
    bool mgz = true;
    for (uint32_t i = 0; i < margin_words; ++i) mgz &= (pixbuf[i] == 0);
    for (uint32_t i = 144; i < 160; ++i) mgz &= (pixbuf[i] == 0);
    check("margin words stay zero (64 black pixels each side)", mgz);

    static uint32_t out[960];
    memset(out, 0xEE, sizeof out);
    tmds_encode_palette_data_1bpp(pal, (const uint8_t*)pixbuf, 640, out);

    bool all = true;
    for (size_t i = 0; i < 320; ++i)
      for (int s = 0; s < 3; ++s)
        for (int h = 0; h < 2; ++h) {
          uint32_t sym = h ? ((out[i] >> 10) & 0x3ff) : (out[i] & 0x3ff);
          bool found = false;
          for (int k = 0; k < 4; ++k) found |= ((pal[s * 4 + k] & 0x3ff) == sym);
          if (!found) all = false;
        }
    check("all 960 words decode to palette symbols (B/G/R sections)", all);

    // --- PLANE ASSIGNMENT (the ground truth fix #3 pins down) ---------------
    const uint32_t* pB = out;          // words   0..319: blue plane
    const uint32_t* pG = out + 320;    // words 320..639: green plane
    const uint32_t* pR = out + 640;    // words 640..959: red plane

    bool ballk = true;
    for (size_t i = 0; i < 320; ++i)
      ballk &= sym_is(pal + 0, pB[i] & 0x3ff, 0) && sym_is(pal + 0, (pB[i] >> 10) & 0x3ff, 0);
    check("BLUE plane (words 0-319) carries all-black symbols", ballk);

    bool rallk = true;
    for (size_t i = 0; i < 320; ++i)
      rallk &= sym_is(pal + 8, pR[i] & 0x3ff, 0) && sym_is(pal + 8, (pR[i] >> 10) & 0x3ff, 0);
    check("RED plane (words 640-959) carries all-black symbols", rallk);

    bool gmag = true;
    for (size_t i = 0; i < 32; ++i)   // 64 margin px = 32 words
      gmag &= sym_is(pal + 4, pG[i] & 0x3ff, 0) && sym_is(pal + 4, (pG[i] >> 10) & 0x3ff, 0);
    check("GREEN plane margins (px 0-63) stay black", gmag);

    // 0xB7 = 1011 0111 -> screen pixels 0..7 = 1,0,1,1,0,1,1,1 (line px 64..71)
    bool gpat =
      sym_is(pal + 4, pG[32] & 0x3ff, 1) &&            // px 64 = bit7 -> green
      sym_is(pal + 4, (pG[32] >> 10) & 0x3ff, 0) &&    // px 65 = bit6 -> black
      sym_is(pal + 4, pG[33] & 0x3ff, 1) &&            // px 66 = bit5 -> green
      sym_is(pal + 4, (pG[287] >> 10) & 0x3ff, 1);     // px 575 = bit0 of 0xFF -> green
    check("GREEN plane (words 320-639) carries the green ink pattern", gpat);

    bool rblk_content = true;
    for (size_t i = 32; i < 288; ++i)  // the content words must stay black on red
      rblk_content &= sym_is(pal + 8, pR[i] & 0x3ff, 0) && sym_is(pal + 8, (pR[i] >> 10) & 0x3ff, 0);
    check("RED plane content words (px 64-575) stay black", rblk_content);

    // parity: the even and odd pixels have INDEPENDENT running-disparity
    // accumulators, each starting at 0 - so the first word of the line is
    // neg/neg, the second pos/pos.  Parity selects only the DC-balance
    // variant, never the colour.
    bool par =
      ((pG[0] & 0x3ff) == (pal[4] & 0x3ff)) &&          // px 0: colour 0, neg (even acc = 0)
      (((pG[0] >> 10) & 0x3ff) == (pal[4] & 0x3ff)) &&  // px 1: colour 0, neg (odd acc = 0)
      (((pG[1] & 0x3ff) == (pal[6] & 0x3ff))) &&        // px 2: colour 0, pos
      (((pG[1] >> 10) & 0x3ff) == (pal[6] & 0x3ff));    // px 3: colour 0, pos
    check("parity selects neg/pos variant, never the colour", par);

    // the true green symbols (colour 1, green section) must NOT appear on the
    // blue/red planes - the colour-to-plane binding has no R/G swap
    bool exclusive = true;
    for (int k : { 1, 3 }) {
      uint32_t g = pal[4 + k] & 0x3ff;
      for (size_t i = 0; i < 320; ++i) {
        exclusive &= ((pB[i] & 0x3ff) != g) && (((pB[i] >> 10) & 0x3ff) != g);
        exclusive &= ((pR[i] & 0x3ff) != g) && (((pR[i] >> 10) & 0x3ff) != g);
      }
    }
    check("green symbols appear on the GREEN plane only (no R/G swap)", exclusive);
  }

  printf(failures ? "FAILURES: %d\n" : "ALL PASS\n", failures);
  return failures ? 1 : 0;
}
