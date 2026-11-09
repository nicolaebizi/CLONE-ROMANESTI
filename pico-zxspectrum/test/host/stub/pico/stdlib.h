// Minimal host stand-in for the Pico SDK header, enough to unit test portable code
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define __not_in_flash_func(f) f
#define __mul_instruction(a, b) ((a) * (b))
