// Host-test stub for the FatFs API subset ZxSpectrumCf uses. The
// implementation lives in test_cf.cpp and backs the "image file" with a
// plain RAM array, with per-call failure injection (failWrite/failSync/
// failSeek counters).
#pragma once

#include <stdint.h>
#include <stddef.h>

typedef unsigned int UINT;
typedef uint32_t FSIZE_t;
typedef unsigned char BYTE;

typedef enum {
  FR_OK = 0,
  FR_DISK_ERR,
  FR_NO_FILE,
  FR_EXIST,
  FR_WRITE_PROTECTED,
  FR_INVALID_PARAMETER
} FRESULT;

#define FA_READ            0x01
#define FA_WRITE           0x02
#define FA_OPEN_EXISTING   0x00
#define FA_CREATE_NEW      0x04
#define FA_CREATE_ALWAYS   0x08

// objsize mirrors the real FatFs FIL (f_size reads it).
typedef struct { FSIZE_t objsize; } FIL;

FRESULT f_open(FIL* fp, const char* path, unsigned mode);
FRESULT f_close(FIL* fp);
FRESULT f_sync(FIL* fp);
FRESULT f_lseek(FIL* fp, FSIZE_t ofs);
FRESULT f_read(FIL* fp, void* buff, UINT btr, UINT* br);
FRESULT f_write(FIL* fp, const void* buff, UINT btw, UINT* bw);
FRESULT f_unlink(const char* path);
FRESULT f_truncate(FIL* fp);
#define f_size(fp) ((fp)->objsize)
