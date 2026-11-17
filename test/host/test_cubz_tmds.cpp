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

  // --- 4: CUBZFAST cross-check vs the generic palette path -------------------
  //
  // The dedicated fast encoder uses libdvi's fixed-parity convention
  // (TMDS_FULLRES_NO_DC_BALANCE: even pixel stream always the negative
  // variant, odd stream always the positive).  Word-level identity with the
  // generic path is impossible by construction (its variant choice follows
  // a content-dependent running accumulator), but the two variants of a
  // symbol DECODE to the same byte - so the checks that matter are:
  //   a) every fast symbol is a genuine palette variant (decodes to a
  //      palette colour on its plane);
  //   b) the DECODED pixel stream of the fast path is IDENTICAL to the
  //      decoded pixel stream of the generic palette path, on all 3 planes,
  //      for random patterns and both real timings (640x480: nw=320,
  //      margin=32 words; 720x540: nw=360, margin=52 words);
  //   c) the B/R planes and the green margins are the constant black word.
  {
    uint32_t fast_lut[ZX_CUBZ_TMDS_FAST_LUT_WORDS];
    uint32_t plane_word;
    zx_cubz_tmds_build_fast_lut(fast_lut, &plane_word, pal);
    zx_cubz_tmds_fast_t fast = { fast_lut, plane_word };

    // a) the LUT/constant words decode to exactly the palette entries used
    bool cw = plane_word == ((pal[0] & 0x3ff) | ((pal[2] & 0x3ff) << 10));
    check("fast plane word = blackNeg | blackPos<<10 from the palette", cw);
    check("fast LUT byte 0 == the constant plane word",
          fast_lut[0] == plane_word && fast_lut[1] == plane_word &&
          fast_lut[2] == plane_word && fast_lut[3] == plane_word);

    struct { uint32_t nw, margin, n_pix; } timings[2] = {
      { 320, 32, 640 },   // 640x480x60 (M1-HDMI / M2-AUTO DVI_TIMING)
      { 360, 52, 720 },   // 720x540x50 (other HDMI timing)
    };
    // Simple deterministic LCG for the random patterns.
    uint32_t rng = 0x1234abcd;
    bool dec_ok = true, margins_ok = true, ff_ok = true;
    // Decode a 10-bit symbol to its palette colour on a channel section
    // (4 words: c0 neg, c1 neg, c0 pos, c1 pos); -1 if not a palette symbol.
    auto decode_sym = [&pal](uint32_t sec, uint32_t sym) -> int {
      for (int j = 0; j < 4; ++j)
        if ((pal[sec + j] & 0x3ff) == sym) return (j & 1);  // 0 black, 1 ink
      return -1;
    };
    for (int tv = 0; tv < 2; ++tv) {
      const uint32_t nw = timings[tv].nw;
      const uint32_t margin = timings[tv].margin;
      const uint32_t n_pix = timings[tv].n_pix;
      const uint32_t margin_pix = margin >> 1;  // pixbuf-domain margin words
      static uint32_t outA[3 * 360];
      static uint32_t outB[3 * 360];
      static uint32_t pixbuf2[360];
      static uint8_t vram[0x4000];
      static uint8_t line[ZX_CUBZ_TMDS_SCREEN_BYTES];
      const int trials = 200;
      for (int tr = 0; tr < trials + 2; ++tr) {
        if (tr < trials) {
          for (uint32_t i = 0; i < ZX_CUBZ_TMDS_SCREEN_BYTES; ++i) {
            rng = rng * 1103515245u + 12345u;
            line[i] = (uint8_t)(rng >> 16);
          }
        } else if (tr == trials) {
          memset(line, 0x00, sizeof line);       // all-black edge
        } else {
          memset(line, 0xFF, sizeof line);       // all-green edge
        }
        // Path A: the generic palette path (expansion LUT + palette encode).
        memset(vram, 0xAA, sizeof vram);
        for (uint32_t c = 0; c < ZX_CUBZ_TMDS_SCREEN_BYTES; ++c)
          vram[c << 8] = line[c];
        memset(pixbuf2, 0, n_pix / 2 * sizeof(uint32_t));
        zx_cubz_tmds_fill_line(pixbuf2, margin_pix, lut, vram);
        tmds_encode_palette_data_1bpp(pal, (const uint8_t *)pixbuf2, n_pix, outA);
        // Path B: the fast dedicated encoder (direct LUT).
        zx_cubz_tmds_fill_line_fast(outB, nw, margin, &fast, vram);
        // b) decoded streams must be identical on all 3 planes; a) every
        // fast symbol must be a genuine palette variant.
        for (uint32_t pi = 0; pi < 3 * (n_pix / 2); ++pi) {
          const uint32_t sec = 4 * (pi / (n_pix / 2));  // B, G, R section
          const int decA = decode_sym(sec, outA[pi] & 0x3ff);
          const int decB = decode_sym(sec, outB[pi] & 0x3ff);
          const int decA2 = decode_sym(sec, (outA[pi] >> 10) & 0x3ff);
          const int decB2 = decode_sym(sec, (outB[pi] >> 10) & 0x3ff);
          if (decB < 0 || decB2 < 0 || decA != decB || decA2 != decB2) {
            if (dec_ok) {
              printf("  DECODE MISMATCH tv=%d tr=%d word %u "
                     "(A=%03x/%03x B=%03x/%03x dec %d/%d %d/%d)\n",
                     tv, tr, pi, outA[pi] & 0x3ff, (outA[pi] >> 10) & 0x3ff,
                     outB[pi] & 0x3ff, (outB[pi] >> 10) & 0x3ff,
                     decA, decB, decA2, decB2);
            }
            dec_ok = false;
          }
        }
      }
      // c) B/R planes + green margins = the constant word; after the
      // all-FF edge line the content must differ from it (ink drawn).
      for (uint32_t i = 0; i < nw; ++i) {
        margins_ok &= (outB[i] == plane_word) && (outB[2 * nw + i] == plane_word);
      }
      for (uint32_t i = 0; i < margin; ++i)
        margins_ok &= (outB[nw + i] == plane_word);
      for (uint32_t i = nw - margin; i < nw; ++i)
        margins_ok &= (outB[nw + i] == plane_word);
      ff_ok &= (outB[nw + margin] != plane_word) &&
               (outB[nw + margin + 1] != plane_word);
    }
    check("FAST decoded pixels == palette-path pixels on 404 lines (both timings)", dec_ok);
    check("FAST every symbol is a genuine palette variant", dec_ok);
    check("FAST B/R planes + G margins are the constant black word", margins_ok);
    check("FAST all-FF line differs from the black plane (green ink drawn)", ff_ok);
  }

  printf(failures ? "FAILURES: %d\n" : "ALL PASS\n", failures);
  return failures ? 1 : 0;
}
