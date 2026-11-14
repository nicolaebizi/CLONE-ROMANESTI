// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#pragma once

#include "pico/stdlib.h"
#include "pico/scanvideo.h"
#include "ZxSpectrumAudioDriverEnum.h"

#ifdef __cplusplus
extern "C" {
#endif

void __not_in_flash_func(zx_prepare_scanvideo_scanline)(
  struct scanvideo_scanline_buffer *scanline_buffer, 
  uint32_t y, 
  uint32_t frame,
  uint8_t* screenPtr,
  uint8_t* attrPtr,
  uint8_t borderColor
);

// CoBra 80 characters per line (vram = start of the 16K video RAM).
void __not_in_flash_func(zx_prepare_scanvideo_scanline_cobra80)(
  struct scanvideo_scanline_buffer *scanline_buffer,
  uint32_t y,
  uint32_t frame,
  uint8_t* vram,
  uint8_t borderColor
);

// CUB-Z 512x256 monochrome raster (vram = 16K video RAM, scroll = $F2 latch).
void __not_in_flash_func(zx_prepare_scanvideo_scanline_cubz)(
  struct scanvideo_scanline_buffer *scanline_buffer,
  uint32_t y,
  uint8_t* vram,
  uint8_t scroll
);

void __not_in_flash_func(zx_prepare_scanvideo_blankline)(
  struct scanvideo_scanline_buffer *scanline_buffer
);

#ifdef __cplusplus
}
#endif
