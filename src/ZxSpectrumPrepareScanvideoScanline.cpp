// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#include "ZxSpectrumPrepareScanvideoScanline.h"
#include "pico/scanvideo/composable_scanline.h"
#include "ZxSpectrumDisplay.h"

#define VGA_RGB_555(r,g,b) ((r##UL<<0)|(g##UL<<6)|(b##UL << 11))
#define VGA_BGYR_1111(r,g,b,y) ((y##UL<<2)|(r##UL<<3)|(g##UL<<1)|b##UL)
#define VGA_RGBY_1111(r,g,b,y) ((y##UL<<3)|(r##UL<<2)|(g##UL<<1)|b##UL)
#define VGA_RGB_332(r,g,b) ((r##UL<<5)|(g##UL<<2)|b##UL)
#define VGA_RGB_222(r,g,b) ((r##UL<<4)|(g##UL<<2)|b##UL)

#define X2(a) ((a) | (a << 16))

static uint32_t zxd_colour_words[16] = {
#if defined(VGA_ENC_BGYR_1111)
  X2(VGA_BGYR_1111(0,0,0,0)), // Black
  X2(VGA_BGYR_1111(0,0,1,0)), // Blue
  X2(VGA_BGYR_1111(1,0,0,0)), // Red
  X2(VGA_BGYR_1111(1,0,1,0)), // Magenta
  X2(VGA_BGYR_1111(0,1,0,0)), // Green
  X2(VGA_BGYR_1111(0,1,1,0)), // Cyan
  X2(VGA_BGYR_1111(1,1,0,0)), // Yellow
  X2(VGA_BGYR_1111(1,1,1,0)), // White
  X2(VGA_BGYR_1111(0,0,0,0)), // Bright Black
  X2(VGA_BGYR_1111(0,0,1,1)), // Bright Blue
  X2(VGA_BGYR_1111(1,0,0,1)), // Bright Red
  X2(VGA_BGYR_1111(1,0,1,1)), // Bright Magenta
  X2(VGA_BGYR_1111(0,1,0,1)), // Bright Green
  X2(VGA_BGYR_1111(0,1,1,1)), // Bright Cyan
  X2(VGA_BGYR_1111(1,1,0,1)), // Bright Yellow
  X2(VGA_BGYR_1111(1,1,1,1))  // Bright White
#elif defined(VGA_ENC_RGBY_1111)
  X2(VGA_RGBY_1111(0,0,0,0)), // Black
  X2(VGA_RGBY_1111(0,0,1,0)), // Blue
  X2(VGA_RGBY_1111(1,0,0,0)), // Red
  X2(VGA_RGBY_1111(1,0,1,0)), // Magenta
  X2(VGA_RGBY_1111(0,1,0,0)), // Green
  X2(VGA_RGBY_1111(0,1,1,0)), // Cyan
  X2(VGA_RGBY_1111(1,1,0,0)), // Yellow
  X2(VGA_RGBY_1111(1,1,1,0)), // White
  X2(VGA_RGBY_1111(0,0,0,0)), // Bright Black
  X2(VGA_RGBY_1111(0,0,1,1)), // Bright Blue
  X2(VGA_RGBY_1111(1,0,0,1)), // Bright Red
  X2(VGA_RGBY_1111(1,0,1,1)), // Bright Magenta
  X2(VGA_RGBY_1111(0,1,0,1)), // Bright Green
  X2(VGA_RGBY_1111(0,1,1,1)), // Bright Cyan
  X2(VGA_RGBY_1111(1,1,0,1)), // Bright Yellow
  X2(VGA_RGBY_1111(1,1,1,1))  // Bright White
#elif defined(VGA_ENC_RGB_222)
  X2(VGA_RGB_222(0,0,0)), // Black
  X2(VGA_RGB_222(0,0,2)), // Blue
  X2(VGA_RGB_222(2,0,0)), // Red
  X2(VGA_RGB_222(2,0,2)), // Magenta
  X2(VGA_RGB_222(0,2,0)), // Green
  X2(VGA_RGB_222(0,2,2)), // Cyan
  X2(VGA_RGB_222(2,2,0)), // Yellow
  X2(VGA_RGB_222(2,2,2)), // White
  X2(VGA_RGB_222(0,0,0)), // Bright Black
  X2(VGA_RGB_222(0,0,3)), // Bright Blue
  X2(VGA_RGB_222(3,0,0)), // Bright Red
  X2(VGA_RGB_222(3,0,3)), // Bright Magenta
  X2(VGA_RGB_222(0,3,0)), // Bright Green
  X2(VGA_RGB_222(0,3,3)), // Bright Cyan
  X2(VGA_RGB_222(3,3,0)), // Bright Yellow
  X2(VGA_RGB_222(3,3,3))  // Bright White
#elif defined(VGA_ENC_RGB_332)
  X2(VGA_RGB_332(0,0,0)), // Black
  X2(VGA_RGB_332(0,0,2)), // Blue
  X2(VGA_RGB_332(5,0,0)), // Red
  X2(VGA_RGB_332(5,0,2)), // Magenta
  X2(VGA_RGB_332(0,5,0)), // Green
  X2(VGA_RGB_332(0,5,2)), // Cyan
  X2(VGA_RGB_332(5,5,0)), // Yellow
  X2(VGA_RGB_332(5,5,2)), // White
  X2(VGA_RGB_332(0,0,0)), // Bright Black
  X2(VGA_RGB_332(0,0,3)), // Bright Blue
  X2(VGA_RGB_332(7,0,0)), // Bright Red
  X2(VGA_RGB_332(7,0,3)), // Bright Magenta
  X2(VGA_RGB_332(0,7,0)), // Bright Green
  X2(VGA_RGB_332(0,7,3)), // Bright Cyan
  X2(VGA_RGB_332(7,7,0)), // Bright Yellow
  X2(VGA_RGB_332(7,7,3))  // Bright White  
#else    
  X2(VGA_RGB_555(0, 0, 0 )), // Black
  X2(VGA_RGB_555(0, 0, 20)), // Blue
  X2(VGA_RGB_555(20,0, 0 )), // Red
  X2(VGA_RGB_555(20,0, 20)), // Magenta
  X2(VGA_RGB_555(0, 20,0 )), // Green
  X2(VGA_RGB_555(0, 20,20)), // Cyan
  X2(VGA_RGB_555(20,20,0 )), // Yellow
  X2(VGA_RGB_555(20,20,20)), // White
  X2(VGA_RGB_555(0, 0, 0 )), // Bright Black
  X2(VGA_RGB_555(0, 0, 31)), // Bright Blue
  X2(VGA_RGB_555(31,0, 0 )), // Bright Red
  X2(VGA_RGB_555(31,0, 31)), // Bright Magenta
  X2(VGA_RGB_555(0, 31,0 )), // Bright Green
  X2(VGA_RGB_555(0, 31,31)), // Bright Cyan
  X2(VGA_RGB_555(31,31,0 )), // Bright Yellow
  X2(VGA_RGB_555(31,31,31))  // Bright White
#endif
};

static uint32_t zx_invert_masks[] = {
  0x00,
  0xff
};

inline uint32_t* single_color_run(uint32_t *buf, uint32_t width, uint32_t ci) {
  // | jmp color_run | color | count-3 |  buf[0] =
  *buf++ = COMPOSABLE_COLOR_RUN | (zxd_colour_words[ci] << 16);
  *buf++ = (width - 3 - 2) | (COMPOSABLE_RAW_2P << 16);
  *buf++ = zxd_colour_words[ci];
  return buf;
}

inline uint32_t* end_run(uint32_t *buf) {
  // end with a black pixel
  *buf++ = COMPOSABLE_RAW_1P;
  *buf++ = COMPOSABLE_EOL_SKIP_ALIGN;
  return buf;
}

void __not_in_flash_func(zx_prepare_scanvideo_scanline)(
  struct scanvideo_scanline_buffer *scanline_buffer,
  uint32_t y, 
  uint32_t frame,
  uint8_t* screenPtr,
  uint8_t* attrPtr,
  uint8_t borderColor
) {
  uint32_t* buf = scanline_buffer->data;

  if (y < (24) || y >= (24 + 192)) {
#if DISPLAY_BORDER_PIXELS_LEFT_BLANK      
    buf = single_color_run(buf, DISPLAY_BORDER_PIXELS_LEFT_BLANK << 1, 0);     
#endif
    buf = single_color_run(
      buf, 
      (DISPLAY_BORDER_PIXELS
          - DISPLAY_BORDER_PIXELS_LEFT_BLANK
          - DISPLAY_BORDER_PIXELS_RIGHT_BLANK ) << 1
      , borderColor);

#if DISPLAY_BORDER_PIXELS_RIGHT_BLANK      
    buf = single_color_run(buf, DISPLAY_BORDER_PIXELS_RIGHT_BLANK << 1, 0);     
#endif   
  } 
  else {
#if DISPLAY_BORDER_PIXELS_LEFT_BLANK      
    buf = single_color_run(buf, DISPLAY_BORDER_PIXELS_LEFT_BLANK << 1, 0);     
#endif
    buf = single_color_run(buf, DISPLAY_BORDER_PIXELS_LEFT_COLORED << 1, borderColor);
    
    const uint v = y - 24;
    const uint8_t *s = screenPtr + ((v & 0x7) << 8) + ((v & 0x38) << 2) + ((v & 0xc0) << 5);
    const uint8_t *a = attrPtr+((v>>3)<<5);
    const int m = (frame >> 5) & 1;   
    
    for (int i = 0; i < 32; ++i) {
      uint8_t c = *a++; // Fetch the attribute for the character
      uint8_t p = *s++ ^ zx_invert_masks[(c >> 7) & m]; // fetch a byte of pixel data
      uint8_t bci = (c >> 3) & 0xf; // The background colour index
      uint8_t fci = (c & 7) | (bci & 0x8); // The foreground colour index

      uint32_t cw[2];
      cw[0] = zxd_colour_words[bci]; // The background colour word
      cw[1] = zxd_colour_words[fci]; // The foreground colour word
      
      uint32_t cwb = cw[(p >> 7) & 1];
      *buf++ = COMPOSABLE_RAW_RUN | (cwb << 16);
      *buf++ = (16 - 3) | (cwb << 16);
      *buf++ = cw[(p >> 6) & 1];
      *buf++ = cw[(p >> 5) & 1];
      *buf++ = cw[(p >> 4) & 1];
      *buf++ = cw[(p >> 3) & 1];
      *buf++ = cw[(p >> 2) & 1];
      *buf++ = cw[(p >> 1) & 1];
      *buf++ = cw[(p >> 0) & 1];
    }
    
    buf = single_color_run(buf, DISPLAY_BORDER_PIXELS_RIGHT_COLORED << 1, borderColor);
#if DISPLAY_BORDER_PIXELS_RIGHT_BLANK      
    buf = single_color_run(buf, DISPLAY_BORDER_PIXELS_RIGHT_BLANK << 1, 0);     
#endif    
  }
  buf = end_run(buf);
  
  scanline_buffer->data_used = buf - scanline_buffer->data;
  scanline_buffer->status = SCANLINE_OK;  
}

// One 8 pixel character cell (16 output pixels) from its pixel and attribute bytes.
static inline uint32_t* cobra_cell(uint32_t *buf, uint8_t p, uint8_t c, const int m) {
  p ^= zx_invert_masks[(c >> 7) & m];
  const uint8_t bci = (c >> 3) & 0xf;
  const uint8_t fci = (c & 7) | (bci & 0x8);
  uint32_t cw[2];
  cw[0] = zxd_colour_words[bci];
  cw[1] = zxd_colour_words[fci];
  const uint32_t cwb = cw[(p >> 7) & 1];
  *buf++ = COMPOSABLE_RAW_RUN | (cwb << 16);
  *buf++ = (16 - 3) | (cwb << 16);
  *buf++ = cw[(p >> 6) & 1];
  *buf++ = cw[(p >> 5) & 1];
  *buf++ = cw[(p >> 4) & 1];
  *buf++ = cw[(p >> 3) & 1];
  *buf++ = cw[(p >> 2) & 1];
  *buf++ = cw[(p >> 1) & 1];
  *buf++ = cw[(p >> 0) & 1];
  return buf;
}

// CoBra 80 chr/lin module (mainboard sheet 15): AV13 = AC5 and the horizontal
// border is switched off.  The 32 paper cells (character counter 128-159,
// AC5=0) come from the lower half of the video RAM; the border cells (122-127
// on the left, 160-167 on the right, AC5=1) from the upper half, at the
// columns given by AC0-AC4 (1A-1F left, 00-07 right).  Only the cells that fit
// in this display's border are drawn.
void __not_in_flash_func(zx_prepare_scanvideo_scanline_cobra80)(
  struct scanvideo_scanline_buffer *scanline_buffer,
  uint32_t y,
  uint32_t frame,
  uint8_t* vram,
  uint8_t borderColor
) {
  if (y < (24) || y >= (24 + 192)) {
    zx_prepare_scanvideo_scanline(scanline_buffer, y, frame, vram, vram + 0x1800, borderColor);
    return;
  }
  uint32_t* buf = scanline_buffer->data;
  const uint v = y - 24;
  const uint32_t line = ((v & 0x7) << 8) + ((v & 0x38) << 2) + ((v & 0xc0) << 5);
  const uint32_t attr = 0x1800 + ((v >> 3) << 5);
  const int m = (frame >> 5) & 1;
  const uint32_t lpx = DISPLAY_BORDER_PIXELS_LEFT_COLORED;
  const uint32_t rpx = DISPLAY_BORDER_PIXELS_RIGHT_COLORED;
  const uint32_t lc = lpx / 8 > 6 ? 6 : lpx / 8;
  const uint32_t rc = rpx / 8 > 8 ? 8 : rpx / 8;
  const uint8_t *hi = vram + 0x2000;

#if DISPLAY_BORDER_PIXELS_LEFT_BLANK
  buf = single_color_run(buf, DISPLAY_BORDER_PIXELS_LEFT_BLANK << 1, 0);
#endif
  if (lpx - lc * 8) buf = single_color_run(buf, (lpx - lc * 8) << 1, 0);
  for (uint32_t i = 32 - lc; i < 32; ++i) buf = cobra_cell(buf, hi[line + i], hi[attr + i], m);
  for (uint32_t i = 0; i < 32; ++i) buf = cobra_cell(buf, vram[line + i], vram[attr + i], m);
  for (uint32_t i = 0; i < rc; ++i) buf = cobra_cell(buf, hi[line + i], hi[attr + i], m);
  if (rpx - rc * 8) buf = single_color_run(buf, (rpx - rc * 8) << 1, 0);
#if DISPLAY_BORDER_PIXELS_RIGHT_BLANK
  buf = single_color_run(buf, DISPLAY_BORDER_PIXELS_RIGHT_BLANK << 1, 0);
#endif
  buf = end_run(buf);
  scanline_buffer->data_used = buf - scanline_buffer->data;
  scanline_buffer->status = SCANLINE_OK;
}

// CUB-Z VDT-52S: 512x256 green phosphor raster drawn 1:1 (one CUB-Z pixel per
// output pixel).  The display has 240 lines, the CUB-Z 256: the CP/M text
// rows are 10 lines high (7 glyph lines, 3 blank) and 25 of them fill lines
// 0-249, so the 6 unused bottom lines and the last (blank) line of the first
// ten rows are dropped - every row stays readable and no glyph line is lost.
static uint8_t cubz_lines[240];
static bool cubz_lines_ready = false;

void __not_in_flash_func(zx_prepare_scanvideo_scanline_cubz)(
  struct scanvideo_scanline_buffer *scanline_buffer,
  uint32_t y,
  uint8_t* vram,
  uint8_t scroll
) {
  if (!cubz_lines_ready) {
    uint32_t n = 0;
    for (uint32_t l = 0; l < 250 && n < 240; ++l) {
      if (l < 100 && (l % 10) == 9) continue;
      cubz_lines[n++] = (uint8_t)l;
    }
    cubz_lines_ready = true;
  }
  uint32_t* buf = scanline_buffer->data;
  const uint32_t on = zxd_colour_words[12] & 0xffff;
  const uint32_t off = zxd_colour_words[0] & 0xffff;
  const uint32_t pair[4] = { off | (off << 16), on | (off << 16), off | (on << 16), on | (on << 16) };
  const uint32_t margin = (DISPLAY_WIDTH_PIXELS - 512) >> 1;
  const uint8_t *s = vram + ((cubz_lines[y < 240 ? y : 239] + scroll) & 255);

  if (margin) buf = single_color_run(buf, margin, 0);
  const uint8_t b0 = s[0];
  *buf++ = COMPOSABLE_RAW_RUN | (((b0 & 0x80) ? on : off) << 16);
  *buf++ = (512 - 3) | (((b0 & 0x40) ? on : off) << 16);
  *buf++ = pair[((b0 >> 5) & 1) | ((b0 >> 3) & 2)];
  *buf++ = pair[((b0 >> 3) & 1) | ((b0 >> 1) & 2)];
  *buf++ = pair[((b0 >> 1) & 1) | ((b0 << 1) & 2)];
  for (uint32_t c = 1; c < 64; ++c) {
    const uint8_t b = s[c << 8];
    *buf++ = pair[((b >> 7) & 1) | ((b >> 5) & 2)];
    *buf++ = pair[((b >> 5) & 1) | ((b >> 3) & 2)];
    *buf++ = pair[((b >> 3) & 1) | ((b >> 1) & 2)];
    *buf++ = pair[((b >> 1) & 1) | ((b << 1) & 2)];
  }
  if (margin) buf = single_color_run(buf, margin, 0);
  buf = end_run(buf);
  scanline_buffer->data_used = buf - scanline_buffer->data;
  scanline_buffer->status = SCANLINE_OK;
}

void __not_in_flash_func(zx_prepare_scanvideo_blankline)(
  struct scanvideo_scanline_buffer *scanline_buffer
) {
  uint32_t* buf = scanline_buffer->data;
  buf = single_color_run(buf, DISPLAY_WIDTH_PIXELS, 0);
  buf = end_run(buf);  
  scanline_buffer->data_used = buf - scanline_buffer->data;
  scanline_buffer->status = SCANLINE_OK;  
}
