// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#include "ZxSpectrumPrepareDviScanline.h"
#include "ZxCubzTmdsEncode.h"
#include <string.h>

extern "C" {
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"
#include "tmds_encode.h"
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
// margins ((active width - 512) / 2 black pixels each side), same colours -
// black background, bright green ink.
//
// Encoded with libdvi's full-resolution 1bpp palette path
// (tmds_setup_palette24_symbols + tmds_encode_palette_data with
// palette_bits = 1, the proven encode of the mandel-full / vista-palette DVI
// apps): the line is expanded to one palette-index byte per pixel (2-entry
// palette {black, bright green}, 64 black margin pixels each side) and
// encoded in one pass to 3 x (width / 2) TMDS words, one 10-bit symbol per
// pixel per channel.
//
// The previous revision was broken two ways: it translated the colour through
// attr_to_colour_chan_X[0xC0], but $C0 (bright green ink on black paper) maps
// to the all-black palette entry there (black drawn on black), and it selected
// the words of palettised_2bpp_tables, which carry the same colour on both
// pixels of a word (pixel doubling), so single-pixel patterns garbled.  It
// also used 32-pixel margins, leaving the tail of the scanline buffer
// unwritten on every line.
//
// The revision on top of that had the plane layout right - blue at word 0,
// green at words_per_channel, red at 2 * words_per_channel, exactly the
// three DMA planes libdvi serves (dvi_timing.c reads lane i from tmdsbuf +
// i * h_active_pixels / DVI_SYMBOLS_PER_WORD; lane 0 is blue - TMDS_SYNC_LANE,
// the "solid red scanline" error pattern is B/G 0x00 + R 0xfc - and the
// proven zx encoders index attr_to_colour_chan_B/G/R for planes 0/1/2) - and
// the palette24 argument order right (blue = bits 0-7, green = bits 8-15,
// red = bits 16-23 of the 24-bit value, per tmds_setup_palette24_symbols).
// What it got wrong was the pixbuf byte format: the palette loop's interp
// lane 0 mask keeps its masked bits in place, so with palette_bits = 1 the
// hardware extracts bit 0 of each pixbuf byte as the colour index, while the
// LUT had put the index in bit 2 - every pixel read back as colour 0 and the
// screen encoded all black (the "few red stripes" were libdvi's solid-red
// late-scanline error lines, not a plane swap).
void __not_in_flash_func(zx_prepare_hdmi_scanline_cubz)(
  struct dvi_inst *dvi,
  uint32_t y,
  uint8_t* vram,
  uint8_t scroll
) {
  // 2-entry 24-bit palette translated to TMDS symbols: per channel 4 words
  // (colour 0 negative/positive disparity, colour 1 negative/positive).
  static uint32_t cubz_palette[12];
  // VRAM byte -> 8 packed pixel bytes (2 words), palette index in bit 0
  // (the LSB - what the palette loop's interp lane 0 mask extracts for
  // palette_bits = 1), MSB = leftmost pixel (same bit order as the VGA
  // encoder).
  static uint32_t cubz_lut[ZX_CUBZ_TMDS_LUT_WORDS];
  // One palette-index byte per pixel, word aligned.  The black margins are
  // set once and never touched.
  static uint32_t cubz_linepix[ZX_CUBZ_TMDS_LINE_WORDS];
  static uint32_t cubz_margin_words;  // margin size in words (per side)
  static bool cubz_ready = false;

  if (!cubz_ready) {
    // ZX bright green on black (24-bit RGB: red<<16 | green<<8 | blue), the
    // same colours as the VGA twin's zxd_colour_words[12] on
    // zxd_colour_words[0].
    const uint32_t palette24[2] = { 0x000000, 0x00ff00 };
    tmds_setup_palette24_symbols(palette24, cubz_palette, 2);
    zx_cubz_tmds_build_lut(cubz_lut);
    memset(cubz_linepix, 0, sizeof(cubz_linepix));
    cubz_margin_words =
      (dvi->timing->h_active_pixels - 512) >> 3;  // (margin px per side) / 4
    cubz_ready = true;
  }

  zx_cubz_lines_init();

  uint32_t *tmdsbuf;
  queue_remove_blocking(&dvi->q_tmds_free, &tmdsbuf);
  const uint8_t *s = vram + ((cubz_lines[y < 240 ? y : 239] + scroll) & 255);

  // Refill only the 512 screen pixels (64 VRAM bytes, byte-per-8-px stride,
  // identical indexing to the VGA encoder); margins stay colour 0.
  zx_cubz_tmds_fill_line(cubz_linepix, cubz_margin_words, cubz_lut, s);
  // One 10-bit TMDS symbol per pixel per channel: width/2 words per plane,
  // planes in order blue, green, red - exactly the full scanline buffer.
  tmds_encode_palette_data(
    cubz_linepix, cubz_palette, tmdsbuf, dvi->timing->h_active_pixels, 1
  );

  queue_add_blocking(&dvi->q_tmds_valid, &tmdsbuf);
}
