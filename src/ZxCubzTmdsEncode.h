// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
// CUB-Z 1bpp -> libdvi palette-pixel expansion, shared by the DVI/HDMI
// scanline encoder (ZxSpectrumPrepareDviScanline.cpp) and the host unit test
// (test/host/test_cubz_tmds.cpp).
#ifndef _ZX_CUBZ_TMDS_ENCODE_H
#define _ZX_CUBZ_TMDS_ENCODE_H

#include <stdint.h>
#include <string.h>

// 256 VRAM bytes -> 2 packed pixel words each.
#define ZX_CUBZ_TMDS_LUT_WORDS 512
// 720 pixels max (dvi_timing_720x540p_50hz), one palette-index byte per pixel.
#define ZX_CUBZ_TMDS_LINE_WORDS 180
// 512 screen pixels = 64 VRAM bytes per line.
#define ZX_CUBZ_TMDS_SCREEN_BYTES 64

// Expand one CUB-Z VRAM byte (8 pixels, MSB = leftmost, same bit order as the
// VGA encoder) into the pixel format tmds_encode_palette_data reads with
// palette_bits = 1: one byte per pixel carrying the 2-entry palette index in
// its LSB (bit 0).  The palette loop's interp lane 0 mask keeps its masked
// bits in place over the word the body has already shifted (pixbuf word <<2
// or >>14), so the LUT byte address contribution is (byte LSB) << 2 = 4 *
// index bytes = word `index` inside the channel section of the palette
// (tmds_setup_palette24_symbols writes colour i at word i, positive
// disparity at word i + n_palette; lane 1 adds the running-disparity sign
// shifted to bit (palette_bits + 2) = +8 bytes = +n_palette words).  With the
// index in bit 2 instead, the hardware read bit 0 = 0 for every pixel and the
// whole screen encoded as colour 0 (all black).  Pixels fill the 32-bit
// words lowest byte first, so the leftmost pixel of each VRAM byte also ends
// up at the lowest pixel position of the line.
static inline void zx_cubz_tmds_build_lut(uint32_t *lut) {
  for (uint32_t b = 0; b < 256; ++b) {
    uint32_t w0 = 0, w1 = 0;
    for (uint32_t j = 0; j < 4; ++j) {
      w0 |= ((b >> (7 - j)) & 1) << (j * 8);
      w1 |= ((b >> (3 - j)) & 1) << (j * 8);
    }
    lut[b << 1] = w0;
    lut[(b << 1) + 1] = w1;
  }
}

// Refill the 512 screen pixels of a packed line buffer.  The margins are
// colour 0 (black) and are set once at init; only the middle is written here.
// vram_line points at the already row-remapped/scrolled VRAM byte 0; the
// byte-per-8-px stride (vram_line[c << 8]) is identical to the VGA encoder's
// indexing.
static inline void zx_cubz_tmds_fill_line(
  uint32_t *pixbuf, uint32_t margin_words, const uint32_t *lut,
  const uint8_t *vram_line
) {
  uint32_t *q = pixbuf + margin_words;
  for (uint32_t c = 0; c < ZX_CUBZ_TMDS_SCREEN_BYTES; ++c) {
    const uint32_t *e = lut + (vram_line[c << 8] << 1);
    *q++ = *e++;
    *q++ = *e;
  }
}

// ---------------------------------------------------------------------------
// FAST dedicated encoder (CUBZFAST): the generic tmds_encode_palette_data
// path (pixbuf expansion + per-pixel palette loop) takes ~46 us/line at the
// 252 MHz scanline clock - at the deadline - so SD/disk activity stealing
// cycles makes libdvi emit its solid-red late-scanline error lines during
// CP/M DIR storms.  This replaces the per-line palette loop with a direct
// VRAM-byte -> TMDS-words lookup.
//
// Why a plain per-byte LUT cannot be WORD-identical to the generic path:
// the palette loop picks the DC-balance variant (negative/positive
// disparity) from the sign of a running accumulator whose value depends on
// the CONTENT (the black symbols carry top-field imbalance -8/+10, the
// green ones -8/+6, so the accumulator drifts differently over black vs
// green runs - the all-black stream cycles with period 10).  The two
// variants of a symbol DECODE to the same byte on the sink, so the variant
// choice is invisible to the display; only the DC balance differs.
//
// This encoder therefore uses libdvi's own fixed-parity convention
// (tmds_encode.S, TMDS_FULLRES_NO_DC_BALANCE): even pixel stream always
// takes the NEGATIVE variant, odd stream always the POSITIVE variant.
// Per line the aggregate DC is then exactly balanced (equal counts of
// even/odd symbols with +-8 imbalance each), parity is per-pixel-position
// only, and the encode becomes static:
//
//  - BLUE and RED planes: both colours carry byte 0x00 on those channels,
//    so every word is ONE constant: blackNeg | blackPos << 10
//    (content-independent, and identical for B and R).
//  - GREEN plane: VRAM byte -> the 4 words of its 8 pixels, word k pairing
//    pixel 2k (even, negative variant) with pixel 2k+1 (odd, positive);
//    256 entries x 4 words = ZX_CUBZ_TMDS_FAST_LUT_WORDS (4 KB).
//  - GREEN margins: the same constant word as the B/R planes.
//
// The table is DERIVED from the real palette symbol table
// (tmds_setup_palette24_symbols output - the palette encode's own symbol
// generator), not hand-computed; the host test cross-checks the fast path
// against the generic palette path on random patterns: every fast symbol
// must be a genuine palette variant and the DECODED pixel streams must be
// identical on all 3 planes (word-level identity is impossible per the
// content-dependent variant selection above).
//
// NOTE: at full-res 1bpp a TMDS word holds 2 symbols (2 x 10 bit), so the
// 8 pixels of a VRAM byte map to 4 words per plane - the fast LUT is 256 x 4
// words, not 256 x 2.

// 256 VRAM bytes -> 4 TMDS words per plane each (2 symbols per word).
#define ZX_CUBZ_TMDS_FAST_LUT_WORDS 1024

// Derive the fast LUT (1024 words) and the constant plane word from the
// REAL palette symbol table produced by tmds_setup_palette24_symbols for
// n_palette = 2: per channel section of 4 words (colour 0 negative,
// colour 1 negative, colour 0 positive, colour 1 positive), sections in
// order blue, green, red.  Entry [b * 4 + k] renders byte b's pixel pair k
// (pixel 2k = even stream, negative variant; pixel 2k+1 = odd stream,
// positive variant), MSB = leftmost pixel.  *plane_word is the
// content-independent word for the blue/red planes and the green margins.
static inline void zx_cubz_tmds_build_fast_lut(
  uint32_t *lut, uint32_t *plane_word, const uint32_t *pal
) {
  // Black = colour 0, green ink = colour 1.  The blue component byte is
  // 0x00 for both colours, so the blue/red symbols are the black ones.
  const uint32_t black_neg = pal[0] & 0x3ff;   // blue section, colour 0, neg
  const uint32_t black_pos = pal[2] & 0x3ff;  // blue section, colour 0, pos
  const uint32_t ink_neg   = pal[5] & 0x3ff;  // green section, colour 1, neg
  const uint32_t ink_pos   = pal[7] & 0x3ff;  // green section, colour 1, pos
  *plane_word = black_neg | (black_pos << 10);
  for (uint32_t b = 0; b < 256; ++b) {
    for (uint32_t k = 0; k < 4; ++k) {
      const uint32_t ce = (b >> (7 - 2 * k)) & 1u;  // even pixel (neg)
      const uint32_t co = (b >> (6 - 2 * k)) & 1u;  // odd pixel (pos)
      lut[(b << 2) | k] =
        (ce ? ink_neg : black_neg) | ((co ? ink_pos : black_pos) << 10);
    }
  }
}

// Per-line state of the fast encoder.
typedef struct {
  const uint32_t *lut;  // 1024 words, byte*4 + word k
  uint32_t plane_word;  // constant word of the B/R planes and G margins
} zx_cubz_tmds_fast_t;

// Fast per-line encode: writes the FULL 3-plane TMDS scanline buffer
// directly (no pixbuf, no palette loop): blue/red = the constant plane
// word, green = margins (the same word) + 64 LUT byte lookups.  nw =
// h_active_pixels / 2, margin_words = (nw - 256) / 2 words per side.
// vram_line points at the already row-remapped/scrolled VRAM byte 0 (the
// same byte-per-8-px stride as the generic path).
static inline void zx_cubz_tmds_fill_line_fast(
  uint32_t *tmds, uint32_t nw, uint32_t margin_words,
  const zx_cubz_tmds_fast_t *f, const uint8_t *vram_line
) {
  uint32_t *pB = tmds;
  uint32_t *pG = tmds + nw;
  uint32_t *pR = tmds + 2 * nw;
  const uint32_t w = f->plane_word;
  // Blue and red planes are content-independent (both colours carry byte
  // 0x00 on those channels): the same constant word everywhere.
  for (uint32_t i = 0; i < nw; ++i) {
    pB[i] = w;
  }
  for (uint32_t i = 0; i < nw; ++i) {
    pR[i] = w;
  }
  // Green margins: the same constant black word.
  for (uint32_t i = 0; i < margin_words; ++i) {
    pG[i] = w;
  }
  for (uint32_t i = nw - margin_words; i < nw; ++i) {
    pG[i] = w;
  }
  // Green content: 64 VRAM bytes -> 4 words each, MSB = leftmost pixel.
  uint32_t *q = pG + margin_words;
  const uint32_t *lut = f->lut;
  for (uint32_t c = 0; c < ZX_CUBZ_TMDS_SCREEN_BYTES; ++c) {
    const uint32_t *e = lut + (vram_line[c << 8] << 2);
    q[0] = e[0];
    q[1] = e[1];
    q[2] = e[2];
    q[3] = e[3];
    q += 4;
  }
}

#endif
