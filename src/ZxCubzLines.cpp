// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#include "ZxSpectrumDisplay.h"

// CUB-Z VDT-52S row remap table, shared by the VGA scanvideo encoder
// (zx_prepare_scanvideo_scanline_cubz) and the DVI/HDMI encoder
// (zx_prepare_hdmi_scanline_cubz).  Compiled into every target through
// zxspectrum_common_src so both video paths can link against it.
//
// The display has 240 lines, the CUB-Z 256: the CP/M text rows are 10 lines
// high (7 glyph lines, 3 blank) and 25 of them fill lines 0-249, so the 6
// unused bottom lines and the last (blank) line of the first ten rows are
// dropped - every row stays readable and no glyph line is lost.
unsigned char cubz_lines[240];

void zx_cubz_lines_init(void) {
  static bool ready = false;
  if (ready) return;
  unsigned n = 0;
  for (unsigned l = 0; l < 250 && n < 240; ++l) {
    if (l < 100 && (l % 10) == 9) continue;
    cubz_lines[n++] = (unsigned char)l;
  }
  ready = true;
}
