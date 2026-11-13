#pragma once
#include "InputStream.h"

// A seekable in-memory stream, standing in for a file on the SD card
class MemInputStream : public InputStream {
  const uint8_t* _buf;
  uint32_t _length;
  uint32_t _i;
public:
  MemInputStream(const uint8_t* buf, uint32_t length) :
    _buf(buf),
    _length(length),
    _i(0)
  {}

  virtual int32_t readByte() { return _i >= _length ? -1 : _buf[_i++]; }
  virtual bool closed() { return false; }
  virtual bool end() { return _i >= _length; }
  virtual int32_t seek(const uint32_t pos) {
    if (pos > _length) return -2;
    _i = pos;
    return _i;
  }
  virtual int32_t rseek(const int32_t rpos) {
    const int64_t p = (int64_t)_i + rpos;
    if (p < 0 || p > _length) return -2;
    _i = (uint32_t)p;
    return _i;
  }
  virtual uint32_t pos() { return _i; }
};
