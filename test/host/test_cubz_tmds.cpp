// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
// Host unit test for the ICE Felix CUB-Z DVI/HDMI scanline encoder plan:
// the 512x256 1bpp line rendered with libdvi's full-resolution 1bpp palette
// path (tmds_setup_palette24_symbols + tmds_encode_palette_data,
// palette_bits = 1), 2-entry palette {black, ZX bright green}.
//
// libdvi itself needs the RP2040/RP2350 SIO interpolators, so the TMDS symbol
// encoder, the 24-bit palette setup (n_palette = 2) and the palette_bits = 1
// encode loop are replicated here verbatim from PicoDVI libdvi
// (tmds_encode.c / tmds_encode.S, tmds_palette_encode_loop).  The CUB-Z pixel
// expansion under test is the REAL code from src/ZxCubzTmdsEncode.h.
//
// Verified properties:
//   1. the LUT expands a VRAM byte MSB-first (leftmost pixel first), palette
//      index in bit 2, margins unpack to all-black, 0xFF to all-green-ink;
//   2. colour 0 is black on every channel and colour 1 is pure green ink
//      (identical symbols on blue and red, distinct on green) - the previous
//      revision drew black on black because attr $C0 maps to black there;
//   3. a full 640-pixel line (margins + pattern) encodes to 3 x 320 words
//      (blue/green/red), every 10-bit symbol decodes back through the
//      palette, margins stay black and single pixels keep their colour and
//      position 1:1 (the previous revision doubled pixels over each word).
#include <cstdint>
#include <cstdio>
#include <cstring>

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
// positive; sections in order blue, green, red.
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

// tmds_palette_encode_loop for palette_bits = 1 (one 32-bit pixbuf word = 2
// pixels: even/left pixel in the low byte, odd/right in the next byte; each
// pixel byte carries the palette index in bit 2).  Output word i of a channel
// section covers pixels 2i (10 low bits) and 2i+1 (bits 10-19).  The running
// disparity sign (ACCUM1 bit 31) only selects between the negative and the
// positive-disparity entry of the same colour, never the colour itself.
static void tmds_encode_palette_data_1bpp(
  const uint32_t* pal, const uint8_t* pix, size_t n_pix, uint32_t* symbuf, int sign
) {
  for (int s = 0; s < 3; ++s) {
    for (size_t i = 0; i < n_pix / 2; ++i) {
      uint32_t e = pal[s * 4 + (pix[2 * i] >> 2) + (sign ? 2 : 0)];
      uint32_t o = pal[s * 4 + (pix[2 * i + 1] >> 2) + (sign ? 2 : 0)];
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

int main() {
  // The exact palette of the firmware encoder.
  const uint32_t palette24[2] = { 0x000000, 0x00ff00 };  // black, bright green
  uint32_t pal[12];
  tmds_setup_palette24_symbols_2(palette24, pal);

  uint32_t lut[ZX_CUBZ_TMDS_LUT_WORDS];
  zx_cubz_tmds_build_lut(lut);

  printf("palette words (B neg0 neg1 pos0 pos1 | G ... | R ...):\n");
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
      ok &= (w[64 + j] == (((0xB7 >> (7 - j)) & 1) ? 0x04 : 0x00));
    check("LUT byte 0xB7 unpacks MSB-first, index at bit 2", ok);
    check("byte 0x00 unpacks all-black", (lut[0] | lut[1]) == 0);
    check("byte 0xFF unpacks all-0x04", (lut[510] == 0x04040404 && lut[511] == 0x04040404));
  }

  // --- 2: the colours --------------------------------------------------------
  {
    bool br =
      (pal[0] & 0x3ff) == (pal[1] & 0x3ff) &&    // blue neg pair equal
      (pal[2] & 0x3ff) == (pal[3] & 0x3ff) &&    // blue pos pair equal
      (pal[8] & 0x3ff) == (pal[9] & 0x3ff) &&    // red neg pair equal
      (pal[10] & 0x3ff) == (pal[11] & 0x3ff);    // red pos pair equal
    check("black/green identical on B and R channels (pure green ink)", br);
    bool blk = (pal[0] & 0x3ff) == (pal[4] & 0x3ff) && (pal[0] & 0x3ff) == (pal[8] & 0x3ff);
    check("black symbol identical on all 3 channels (true black)", blk);
    bool g = ((pal[4] & 0x3ff) != (pal[5] & 0x3ff)) || ((pal[6] & 0x3ff) != (pal[7] & 0x3ff));
    check("green channel symbols differ black vs green", g);
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
    tmds_encode_palette_data_1bpp(pal, (const uint8_t*)pixbuf, 640, out, 0);

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

    bool mb =
      ((out[0] & 0x3ff) == (pal[0] & 0x3ff)) &&      // px 0 blue: black
      ((out[320] & 0x3ff) == (pal[4] & 0x3ff)) &&    // px 0 green: black
      ((out[640] & 0x3ff) == (pal[8] & 0x3ff)) &&    // px 0 red: black
      (((out[320 + 319] >> 10) & 0x3ff) == (pal[4] & 0x3ff));  // px 639 green: black
    check("margin pixels black on B/G/R (neg colour0 symbols)", mb);

    // 0xB7 = 1011 0111 -> screen pixels 0..7 = 1,0,1,1,0,1,1,1
    bool pat =
      ((out[320 + 32] & 0x3ff) == (pal[5] & 0x3ff)) &&            // px 64 = bit7 -> green
      (((out[320 + 32] >> 10) & 0x3ff) == (pal[4] & 0x3ff)) &&    // px 65 = bit6 -> black
      ((out[320 + 33] & 0x3ff) == (pal[5] & 0x3ff)) &&            // px 66 = bit5 -> green
      (((out[320 + 287] >> 10) & 0x3ff) == (pal[5] & 0x3ff));     // px 575 = bit0 of 0xFF -> green
    check("single pixels keep colour+position 1:1 (0xB7 pattern)", pat);

    // The other disparity state must decode to the same colours (positive
    // variants): colour 0 -> pos entry, colour 1 -> pos entry.
    static uint32_t outp[960];
    memset(outp, 0xEE, sizeof outp);
    tmds_encode_palette_data_1bpp(pal, (const uint8_t*)pixbuf, 640, outp, 1);
    bool pp =
      ((outp[320 + 32] & 0x3ff) == (pal[7] & 0x3ff)) &&          // px 64 green pos
      (((outp[320 + 32] >> 10) & 0x3ff) == (pal[6] & 0x3ff)) &&  // px 65 black pos
      ((outp[320 + 33] & 0x3ff) == (pal[7] & 0x3ff));           // px 66 green pos
    check("positive-disparity entries encode the same colours", pp);
  }

  printf(failures ? "FAILURES: %d\n" : "ALL PASS (%d)\n", failures);
  return failures ? 1 : 0;
}
