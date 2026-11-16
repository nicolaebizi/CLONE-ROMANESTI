// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
// CUB-Z 1bpp -> libdvi palette-pixel expansion, shared by the DVI/HDMI
// scanline encoder (ZxSpectrumPrepareDviScanline.cpp) and the host unit test
// (test/host/test_cubz_tmds.cpp).
#ifndef _ZX_CUBZ_TMDS_ENCODE_H
#define _ZX_CUBZ_TMDS_ENCODE_H

#include <stdint.h>

// 256 VRAM bytes -> 2 packed pixel words each.
#define ZX_CUBZ_TMDS_LUT_WORDS 512
// 720 pixels max (dvi_timing_720x540p_50hz), one palette-index byte per pixel.
#define ZX_CUBZ_TMDS_LINE_WORDS 180
// 512 screen pixels = 64 VRAM bytes per line.
#define ZX_CUBZ_TMDS_SCREEN_BYTES 64

// Expand one CUB-Z VRAM byte (8 pixels, MSB = leftmost, same bit order as the
// VGA encoder) into the pixel format tmds_encode_palette_data reads with
// palette_bits = 1: one byte per pixel carrying the 2-entry palette index in
// bit 2 (index << 2).  Pixels fill the 32-bit words lowest byte first, so the
// leftmost pixel of each VRAM byte also ends up at the lowest pixel position
// of the line.
static inline void zx_cubz_tmds_build_lut(uint32_t *lut) {
  for (uint32_t b = 0; b < 256; ++b) {
    uint32_t w0 = 0, w1 = 0;
    for (uint32_t j = 0; j < 4; ++j) {
      w0 |= ((b >> (7 - j)) & 1) << (j * 8 + 2);
      w1 |= ((b >> (3 - j)) & 1) << (j * 8 + 2);
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

#endif
