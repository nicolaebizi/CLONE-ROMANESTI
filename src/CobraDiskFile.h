// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#pragma once

#include "CobraFloppy.h"
#include "SdCardFatFsSpi.h"
#include "ff.h"

// A CoBra disk image file on the SD card.  Opened read/write when the file
// allows it (writes are synced straight away), read-only otherwise, which the
// drive reports as a write protected disk.
class CobraDiskFile : public CobraDiskMedia {
  FIL _fil;
  bool _open;
  bool _writable;
public:
  CobraDiskFile(SdCardFatFsSpi* sdCard, const char* name);
  virtual ~CobraDiskFile();
  virtual int32_t readAt(uint32_t pos, uint8_t *buf, uint32_t len);
  virtual int32_t writeAt(uint32_t pos, const uint8_t *buf, uint32_t len);
  virtual uint32_t size();
  virtual bool writable() { return _writable; }
  bool isOpen() const { return _open; }
};
