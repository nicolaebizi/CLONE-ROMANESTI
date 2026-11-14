// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#pragma once

#include "pico/stdlib.h"

extern "C" {
#include "dvi.h"
}

#ifdef __cplusplus
extern "C" {
#endif

void __not_in_flash_func(zx_prepare_hdmi_scanline)(
  struct dvi_inst *dvi, 
  uint32_t y, 
  uint32_t frame,
  uint8_t* screenPtr,
  uint8_t* attrPtr,
  uint8_t borderColor
);

// CoBra 80 chr/lin mode scanline (TMDS twin of
// zx_prepare_scanvideo_scanline_cobra80): draws the 32 paper cells from the
// lower video RAM half plus the left/right border cells from the upper half.
void __not_in_flash_func(zx_prepare_hdmi_scanline_cobra80)(
  struct dvi_inst *dvi,
  uint32_t y,
  uint32_t frame,
  uint8_t* vram,
  uint8_t borderColor
);

#ifdef __cplusplus
}
#endif
