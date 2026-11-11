/*
 *  board_config.h - Board configuration for BOOTMAN (standalone boot manager)
 *
 *  Adapted from FRANK C64 (Frodo4) src/board_config.h, Murmulator M1 layout,
 *  trimmed to what the boot manager actually uses (no PSRAM, no C64 timing).
 *
 *  BOOTMAN - standalone boot manager for Murmulator RP2350
 */
#ifndef BOOTMAN_BOARD_CONFIG_H
#define BOOTMAN_BOARD_CONFIG_H

#include "hardware/vreg.h"

/*
 * BOARD_M1 - Murmulator M1 GPIO layout:
 *   VGA/HDMI connector: BASE=6..13 (8 shared pins)
 *   SD:   CLK=2, CMD=3, DAT0=4, DAT3=5
 *   PS/2: CLK=0, DATA=1
 *   NES:  CLK=14, LATCH=15, DATA=16
 */
#ifndef BOARD_M1
#define BOARD_M1
#endif

#ifndef CPU_CLOCK_MHZ
#define CPU_CLOCK_MHZ 378
#endif

#ifndef CPU_VOLTAGE
#define CPU_VOLTAGE VREG_VOLTAGE_1_60
#endif

#ifndef FLASH_MAX_FREQ_MHZ
#define FLASH_MAX_FREQ_MHZ 66
#endif

//=============================================================================
// M1 Layout
//=============================================================================

// Video pins: the VGA PIO takes up 8 consecutive outputs starting here
// (same connector as the HDMI differential output on M1 boards).
#define HDMI_BASE_PIN 6

// SD Card Pins (SPI mode)
#define SDCARD_PIN_CLK    2
#define SDCARD_PIN_CMD    3
#define SDCARD_PIN_D0     4
#define SDCARD_PIN_D3     5

// PS/2 Keyboard Pins
#define PS2_PIN_CLK  0
#define PS2_PIN_DATA 1

// NES/SNES Gamepad Pins
#define NESPAD_GPIO_CLK   14
#define NESPAD_GPIO_DATA  16
#define NESPAD_GPIO_LATCH 15

// Bootman frame buffer / text resolution (matches the VGA driver): the
// scan line ISR fetches SCREEN_WIDTH bytes per line, buffer h = SCREEN_HEIGHT
#define BOOTMAN_FB_WIDTH  320
#define BOOTMAN_FB_HEIGHT 240

// Text grid (8x8 MenuFont)
#define BOOTMAN_COLS (BOOTMAN_FB_WIDTH / 8)   // 40
#define BOOTMAN_ROWS (BOOTMAN_FB_HEIGHT / 8)  // 30

#endif // BOOTMAN_BOARD_CONFIG_H
