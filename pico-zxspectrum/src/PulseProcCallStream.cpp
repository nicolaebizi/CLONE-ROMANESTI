// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#include "PulseProcCallStream.h"

PulseProcCallStream::PulseProcCallStream(uint32_t* i) :
  _l(0),
  _i(i),
  _in(0),
  _p(0),
  _active(false)
{}

void PulseProcCallStream::init(PulseProc *nxt, uint32_t l, uint32_t p) {
  next(nxt);
  _l = l;
  _in = *_i;
  _p = p;
  _active = true;
}

void PulseProcCallStream::reset() {
  _l = 0;
  _in = 0;
  _p = 0;
  _active = false;
}

int32_t __not_in_flash_func(PulseProcCallStream::advance)(
  InputStream *is,
  bool *pstate,
  PulseProc **top
) {
  if (_l == 0) {
    // Carry on after the call block, unless this is a return with no call in progress
    if (_active) {
      *_i = _in;
      _active = false;
    }
    return PP_COMPLETE;
  }
  int32_t r;
  r = is->seek(_p);
  if (r < 0) return PP_ERROR;
  r = is->readWord();
  if (r < 0) return PP_ERROR;
  _p = is->pos();
  // Call targets are signed offsets from the call block, which is the one before _in
  *_i = _in - 1 + static_cast<int16_t>(r);
  --_l;
  return PP_COMPLETE;
}
