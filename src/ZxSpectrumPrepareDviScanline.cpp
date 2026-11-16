// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#include "ZxSpectrumPrepareDviScanline.h"

extern "C" {
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"
#include "tmds_encode_zxspectrum.h"
}
#include "ZxSpectrumDisplay.h"

// Screen handler
//
// 256×192 Pixels
// 32x24 Charaters
//
// 240-192 = 48 => 24 border rows top and bottom
// 320-256 = 64 => 64 border pixels left and right
//
void __not_in_flash_func(zx_prepare_hdmi_scanline)(
  struct dvi_inst *dvi, 
  uint32_t y, 
  uint32_t frame,
  uint8_t* screenPtr,
  uint8_t* attrPtr,
  uint8_t borderColor
) {  
  uint32_t *tmdsbuf;
  queue_remove_blocking(&dvi->q_tmds_free, &tmdsbuf);
  uint32_t *p = tmdsbuf;
  const uint v = y - 24;
  const uint8_t *s = screenPtr + ((v & 0x7) << 8) + ((v & 0x38) << 2) + ((v & 0xc0) << 5);
  const uint8_t *a = attrPtr+((v>>3)<<5);
  const int m = (frame >> 5) & 1 ? 0xff : 0x7f;
  
  if (y < 24 || y >= (24+192)) {
    for (int plane = 0; plane < 3; ++plane) {
#if DISPLAY_BORDER_PIXELS_LEFT_BLANK      
      p = tmds_encode_border(
        DISPLAY_BORDER_PIXELS_LEFT_BLANK, // r0 is width in pixels
        plane,                       // r1 is colour channel
        p,                           // r2 is output TMDS buffer
        0                            // r3 is the colour attribute
      );
#endif
      p = tmds_encode_border(
        DISPLAY_BORDER_PIXELS
          - DISPLAY_BORDER_PIXELS_LEFT_BLANK
          - DISPLAY_BORDER_PIXELS_RIGHT_BLANK, // r0 is width in pixels
        plane,                       // r1 is colour channel
        p,                           // r2 is output TMDS buffer
        borderColor                  // r3 is the colour attribute
      );
#if DISPLAY_BORDER_PIXELS_RIGHT_BLANK
      p = tmds_encode_border(
        DISPLAY_BORDER_PIXELS_RIGHT_BLANK, // r0 is width in pixels
        plane,                       // r1 is colour channel
        p,                           // r2 is output TMDS buffer
        0                            // r3 is the colour attribute
      ); 
#endif
    }
  }
  else {
    for (int plane = 0; plane < 3; ++plane) {
#if DISPLAY_BORDER_PIXELS_LEFT_BLANK      
      p = tmds_encode_border(
        DISPLAY_BORDER_PIXELS_LEFT_BLANK, // r0 is width in pixels
        plane,                       // r1 is colour channel
        p,                           // r2 is output TMDS buffer
        0                            // r3 is the colour attribute
      );      
#endif
      p = tmds_encode_border(
        DISPLAY_BORDER_PIXELS_LEFT_COLORED,  // r0 is width in pixels
        plane,                       // r1 is colour channel
        p,                           // r2 is output TMDS buffer
        borderColor                  // r3 is the colour attribute
      );
      p = tmds_encode_screen(
        (const uint8_t*)s,
        a,
        p,
        m,
        plane
      );  
      p = tmds_encode_border(
        DISPLAY_BORDER_PIXELS_RIGHT_COLORED, // r0 is width in pixels
        plane,                       // r1 is colour channel
        p,                           // r2 is output TMDS buffer
        borderColor                  // r3 is the colour attribute
      );
#if DISPLAY_BORDER_PIXELS_RIGHT_BLANK
      p = tmds_encode_border(
        DISPLAY_BORDER_PIXELS_RIGHT_BLANK, // r0 is width in pixels
        plane,                       // r1 is colour channel
        p,                           // r2 is output TMDS buffer
        0                            // r3 is the colour attribute
      );
#endif
    }
  }
  queue_add_blocking(&dvi->q_tmds_valid, &tmdsbuf);
}

// CoBra 80 chr/lin module (mainboard sheet 15): AV13 = AC5 and the horizontal
// border is switched off.  The 32 paper cells (character counter 128-159,
// AC5=0) come from the lower half of the video RAM; the border cells (122-127
// on the left, 160-167 on the right, AC5=1) from the upper half, at the
// columns given by AC0-AC4 (1A-1F left, 00-07 right).  Only the cells that fit
// in this display's border are drawn.  TMDS twin of
// zx_prepare_scanvideo_scanline_cobra80: same cell layout, one word per
// output pixel pair per channel, encoded with tmds_encode_cobra80_cells for
// the border cells and tmds_encode_screen for the 32 paper cells.
void __not_in_flash_func(zx_prepare_hdmi_scanline_cobra80)(
  struct dvi_inst *dvi,
  uint32_t y,
  uint32_t frame,
  uint8_t* vram,
  uint8_t borderColor
) {
  uint32_t *tmdsbuf;
  queue_remove_blocking(&dvi->q_tmds_free, &tmdsbuf);
  uint32_t *p = tmdsbuf;
  const int m = (frame >> 5) & 1 ? 0xff : 0x7f;

  if (y < 24 || y >= (24+192)) {
    for (int plane = 0; plane < 3; ++plane) {
#if DISPLAY_BORDER_PIXELS_LEFT_BLANK
      p = tmds_encode_border(
        DISPLAY_BORDER_PIXELS_LEFT_BLANK,
        plane,
        p,
        0
      );
#endif
      p = tmds_encode_border(
        DISPLAY_BORDER_PIXELS
          - DISPLAY_BORDER_PIXELS_LEFT_BLANK
          - DISPLAY_BORDER_PIXELS_RIGHT_BLANK,
        plane,
        p,
        borderColor
      );
#if DISPLAY_BORDER_PIXELS_RIGHT_BLANK
      p = tmds_encode_border(
        DISPLAY_BORDER_PIXELS_RIGHT_BLANK,
        plane,
        p,
        0
      );
#endif
    }
  }
  else {
    const uint v = y - 24;
    const uint32_t line = ((v & 0x7) << 8) + ((v & 0x38) << 2) + ((v & 0xc0) << 5);
    const uint32_t attr = 0x1800 + ((v >> 3) << 5);
    const uint32_t lpx = DISPLAY_BORDER_PIXELS_LEFT_COLORED;
    const uint32_t rpx = DISPLAY_BORDER_PIXELS_RIGHT_COLORED;
    const uint32_t lc = lpx / 8 > 6 ? 6 : lpx / 8;
    const uint32_t rc = rpx / 8 > 8 ? 8 : rpx / 8;
    const uint8_t *hi = vram + 0x2000;

    for (int plane = 0; plane < 3; ++plane) {
#if DISPLAY_BORDER_PIXELS_LEFT_BLANK
      p = tmds_encode_border(
        DISPLAY_BORDER_PIXELS_LEFT_BLANK,
        plane,
        p,
        0
      );
#endif
      if (lpx - lc * 8) p = tmds_encode_border(lpx - lc * 8, plane, p, 0);
      p = tmds_encode_cobra80_cells(
        (const uint8_t*)(hi + line + (32 - lc)),
        (const uint8_t*)(hi + attr + (32 - lc)),
        p,
        lc,
        m,
        plane
      );
      p = tmds_encode_screen(
        (const uint8_t*)(vram + line),
        (const uint8_t*)(vram + attr),
        p,
        m,
        plane
      );
      p = tmds_encode_cobra80_cells(
        (const uint8_t*)(hi + line),
        (const uint8_t*)(hi + attr),
        p,
        rc,
        m,
        plane
      );
      if (rpx - rc * 8) p = tmds_encode_border(rpx - rc * 8, plane, p, 0);
#if DISPLAY_BORDER_PIXELS_RIGHT_BLANK
      p = tmds_encode_border(
        DISPLAY_BORDER_PIXELS_RIGHT_BLANK,
        plane,
        p,
        0
      );
#endif
    }
  }
  queue_add_blocking(&dvi->q_tmds_valid, &tmdsbuf);
}

// ICE Felix CUB-Z VDT-52S: 512x256 green phosphor raster drawn 1:1 (one CUB-Z
// pixel per output pixel), row-remapped with the shared cubz_lines[] table and
// scrolled by the $F2 SCROLL latch.  TMDS twin of
// zx_prepare_scanvideo_scanline_cubz: same 512-wide line from _RAM[4], same
// margins ((DISPLAY_WIDTH_PIXELS - 512) / 2 black pixels each side), same
// colours - black background, bright green ink (ZX attribute $C0, matching the
// VGA's zxd_colour_words[12] on zxd_colour_words[0]).  Encoded one word (2
// pixels) at a time per channel with the words of the proven
// palettised_2bpp_tables, selected through the existing
// attr_to_colour_chan_X tables like tmds_encode_screen does.
void __not_in_flash_func(zx_prepare_hdmi_scanline_cubz)(
  struct dvi_inst *dvi,
  uint32_t y,
  uint8_t* vram,
  uint8_t scroll
) {
  static uint32_t cubz_words[3][4];
  static bool cubz_words_ready = false;

  if (!cubz_words_ready) {
    // Bright green ink ($C0: ink = colour 4 green + bright, paper = black),
    // translated to the per-channel b0b1f1f0 palette nibbles exactly like
    // tmds_encode_screen translates an attribute byte.
    const uint8_t attr = 0xC0;
    const uint8_t *chan[3] = {
      attr_to_colour_chan_B, attr_to_colour_chan_G, attr_to_colour_chan_R
    };
    for (int plane = 0; plane < 3; ++plane) {
      const uint32_t *lut =
        (const uint32_t *)(palettised_2bpp_tables + (chan[plane][attr] << 5));
      // lut[0] = off/off, lut[1] = left off/right on,
      // lut[2] = left on/right off, lut[3] = on/on (left pixel = LSB symbol).
      cubz_words[plane][0] = lut[0];
      cubz_words[plane][1] = lut[1];
      cubz_words[plane][2] = lut[2];
      cubz_words[plane][3] = lut[3];
    }
    cubz_words_ready = true;
  }

  zx_cubz_lines_init();

  uint32_t *tmdsbuf;
  queue_remove_blocking(&dvi->q_tmds_free, &tmdsbuf);
  uint32_t *p = tmdsbuf;
  const uint32_t marginL = DISPLAY_BORDER_PIXELS_LEFT;
  const uint32_t marginR = DISPLAY_BORDER_PIXELS_RIGHT;
  const uint8_t *s = vram + ((cubz_lines[y < 240 ? y : 239] + scroll) & 255);

  for (int plane = 0; plane < 3; ++plane) {
    const uint32_t *w = cubz_words[plane];
    if (marginL) p = tmds_encode_border(marginL, plane, p, 0);
    for (uint32_t c = 0; c < 64; ++c) {
      const uint8_t b = s[c << 8];
      *p++ = w[((b >> 6) & 1) | ((b >> 6) & 2)];
      *p++ = w[((b >> 4) & 1) | ((b >> 4) & 2)];
      *p++ = w[((b >> 2) & 1) | ((b >> 2) & 2)];
      *p++ = w[(b & 1) | (b & 2)];
    }
    if (marginR) p = tmds_encode_border(marginR, plane, p, 0);
  }
  queue_add_blocking(&dvi->q_tmds_valid, &tmdsbuf);
}
