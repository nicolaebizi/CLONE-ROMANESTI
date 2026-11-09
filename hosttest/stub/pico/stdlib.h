#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define __not_in_flash_func(f) f
#define __not_in_flash(x)
#define MUL32(a,b) ((a)*(b))
typedef unsigned int uint;
static inline uint32_t time_us_32() { return 0; }
#define __mul_instruction(a,b) ((a)*(b))
