// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#pragma once

#include "PulseProc.h"
#include <pico/stdlib.h>
#include <pico/printf.h>
#include "InputStream.h"
#include <vector>

// Upper bound on the symbol table size (in 16 bit entries) for a TZX generalized data block.
// The format allows 256 * 256 entries (128KB), which would exhaust the heap.
#ifndef PULSE_PROC_MAX_SYMDEFS
#define PULSE_PROC_MAX_SYMDEFS 4096
#endif

class PulseProcSymdefs : public PulseProc {
private:

  std::vector<uint16_t>* _sd;
  uint32_t _np;
  uint32_t _as;

public:

  PulseProcSymdefs(std::vector<uint16_t>* sd);
  
  void init(
    PulseProc *nxt, 
    uint32_t np,
    uint32_t as
  );

  int32_t advance(
    InputStream *is,
    bool *pstate,
    PulseProc **top
  );
  
  inline uint16_t at(uint32_t si, uint32_t pi) {
    uint32_t i = pi + (__mul_instruction(si, (_np + 1)));
    if (i >= _sd->size()) {
      DBG_PULSE("PulseProcSymdefs: symbol index out of range %ld\n", i);
      return 0;
    }
    return _sd->at(i);
  }
  
  inline uint32_t np() {
    return _np;
  }
  
  inline uint32_t as() {
    return _as;
  }  
};
