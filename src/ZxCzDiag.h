// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#pragma once

#include <stdint.h>

// CUB-Z HDMI diag (ZX_CZDIAG, default OFF): live evidence collected by the
// DVI render loop's cubz branch and its scanline encoder, read by the menu
// row "CZD: ...".  Everything (struct, storage, functions) compiles out
// unless the build passes -DZX_CZDIAG=ON, so release binaries are untouched.
#ifdef ZX_CZDIAG

typedef struct ZxCzDiag {
  uint32_t hz;      // (a) cubz-branch calls in the last second (60-frame latch)
  uint32_t hActive; // (b) dvi timing h_active_pixels as the code sees it
  uint8_t  setup;   // (c) tmds_setup_palette24_symbols called
  uint8_t  cubz;    // (d) zxSpectrum.cubz() as the DVI loop sees it
  uint8_t  ob;      // (e) B plane base pointer & 3 (tmdsbuf parity)
  uint8_t  og;      // (e) G plane offset in words & 3 (h_active/2 parity)
} ZxCzDiag;

const ZxCzDiag *zxCzDiag(void);
void zxCzDiagLine(bool cubzPred, uint32_t hActive); // sampled every DVI line
void zxCzDiagHit(void);                             // cubz branch entered
void zxCzDiagFrameEnd(void);                        // frame wrap -> second latch
void zxCzDiagSetupDone(void);                       // palette setup ran
void zxCzDiagPixbuf(const uint32_t *tmdsbuf, uint32_t hActive);

#endif
