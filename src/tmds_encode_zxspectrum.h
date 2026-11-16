// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#ifndef _TMDS_ENCODE_ZXSPECTRUM_H
#define _TMDS_ENCODE_ZXSPECTRUM_H

#include "pico/types.h"

uint32_t * __not_in_flash_func(tmds_encode_screen)(
  const uint8_t *screenRowPtr,
  const uint8_t	*attributeRowPtr,
  uint32_t *tmdsbuf, 
  uint32_t flash, 
  uint32_t plane
);

// Variable length screen encode: ncells 8-pixel cells (one byte of pixel
// data and one byte of attribute per cell). Used for the CoBra 80 chr/lin
// border cells.
uint32_t * __not_in_flash_func(tmds_encode_cobra80_cells)(
  const uint8_t *screenRowPtr,
  const uint8_t	*attributeRowPtr,
  uint32_t *tmdsbuf,
  uint32_t ncells,
  uint32_t flash,
  uint32_t plane
);


uint32_t *__not_in_flash_func(tmds_encode_border)(
  uint32_t nchars,   // r0 is width in characters
  uint32_t plane,    // r1 is colour channel
  uint32_t *tmdsbuf, // r2 is output TMDS buffer
  uint32_t attr      // r3 is the colour attribute
);

// Colour translation tables (attribute byte -> per-channel b0b1f1f0 nibble)
// and the TMDS palette (nibble -> 4 x 2-pixel words, see the .S header).
// Exported for the CUB-Z HDMI scanline encoder.
extern const uint8_t attr_to_colour_chan_B[256];
extern const uint8_t attr_to_colour_chan_G[256];
extern const uint8_t attr_to_colour_chan_R[256];
// 16 nibbles x 4 two-bit crumbs x 2 words (2 pixels per word).
extern const uint32_t palettised_2bpp_tables[128];

#endif
