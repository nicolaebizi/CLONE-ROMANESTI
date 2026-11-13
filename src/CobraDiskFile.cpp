// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#include "CobraDiskFile.h"

CobraDiskFile::CobraDiskFile(SdCardFatFsSpi* sdCard, const char* name) :
  _open(false),
  _writable(false)
{
  if (!sdCard->mounted() && !sdCard->mount()) return;
  if (f_open(&_fil, name, FA_READ | FA_WRITE | FA_OPEN_EXISTING) == FR_OK) {
    _open = true;
    _writable = true;
  }
  else if (f_open(&_fil, name, FA_READ | FA_OPEN_EXISTING) == FR_OK) {
    _open = true;
  }
}

CobraDiskFile::~CobraDiskFile() {
  if (_open) f_close(&_fil);
}

int32_t CobraDiskFile::readAt(uint32_t pos, uint8_t *buf, uint32_t len) {
  if (!_open || f_lseek(&_fil, pos) != FR_OK) return -1;
  UINT br = 0;
  if (f_read(&_fil, buf, len, &br) != FR_OK) return -2;
  return (int32_t)br;
}

int32_t CobraDiskFile::writeAt(uint32_t pos, const uint8_t *buf, uint32_t len) {
  if (!_open || !_writable || f_lseek(&_fil, pos) != FR_OK) return -1;
  UINT bw = 0;
  if (f_write(&_fil, buf, len, &bw) != FR_OK) return -2;
  f_sync(&_fil);
  return (int32_t)bw;
}

uint32_t CobraDiskFile::size() {
  return _open ? (uint32_t)f_size(&_fil) : 0;
}
