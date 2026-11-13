// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#pragma once

#include <pico/stdlib.h>
#include "PulseProc.h"
#include "PulseProcTone.h"

class PulseProcCallStream : public PulseProc {
private:
  uint32_t _l;
  uint32_t* _i;
  uint32_t _in;
  uint32_t _p;
  bool _active;
  
public:

  PulseProcCallStream(uint32_t* i);
  
  void init(PulseProc *next, uint32_t l, uint32_t p);

  // Forget any call sequence in progress, e.g. when a new tape is loaded
  void reset();
  
  virtual int32_t advance(
    InputStream *is,
    bool *pstate,
    PulseProc **top
  );
};
