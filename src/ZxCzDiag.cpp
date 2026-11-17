// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#include "ZxCzDiag.h"

// CUB-Z HDMI diag storage (ZX_CZDIAG, default OFF).  ~20 bytes of .bss in
// the diag build, zero code and zero bytes in every other build.
#ifdef ZX_CZDIAG

static ZxCzDiag zxCzDiagState;
static uint32_t zxCzDiagSecCalls;   // cubz branch calls since the last latch
static uint32_t zxCzDiagSecFrames;  // frames since the last latch

const ZxCzDiag *zxCzDiag(void) { return &zxCzDiagState; }

void zxCzDiagLine(bool cubzPred, uint32_t hActive) {
  zxCzDiagState.cubz = cubzPred ? 1 : 0;   // (d) predicate as the loop sees it
  zxCzDiagState.hActive = hActive;         // (b) h_active as the code sees it
}

void zxCzDiagHit(void) { ++zxCzDiagSecCalls; }

void zxCzDiagFrameEnd(void) {
  // Per-second counter: the branch calls of the last second, latched on
  // every 60th frame (~1 s at 640x480x60) and reset for the next second.
  if (++zxCzDiagSecFrames >= 60) {
    zxCzDiagState.hz = zxCzDiagSecCalls;
    zxCzDiagSecCalls = 0;
    zxCzDiagSecFrames = 0;
  }
}

void zxCzDiagSetupDone(void) { zxCzDiagState.setup = 1; }

void zxCzDiagPixbuf(const uint32_t *tmdsbuf, uint32_t hActive) {
  zxCzDiagState.ob = (uint8_t)(((uintptr_t)tmdsbuf) & 3); // B plane base %4
  zxCzDiagState.og = (uint8_t)((hActive >> 1) & 3);       // G offset (words) %4
}

#endif
