/*
 *  vga_bootman.h - minimal 320x240 8bpp VGA scanout for BOOTMAN
 *
 *  Adapted from FRANK C64 drivers/vga.h + c64/src/board_config.h.
 *  BOOTMAN - standalone boot manager for Murmulator RP2350
 */
#pragma once

#include "../src/board_config.h"

#define PIO_VGA (pio0)
#ifndef VGA_BASE_PIN
#define VGA_BASE_PIN HDMI_BASE_PIN
#endif
#define VGA_DMA_IRQ (DMA_IRQ_0)

#define RGB888(r, g, b) ( ((r) << 16) | ((g) << 8 ) | (b) )

#define SCREEN_WIDTH (320)
#define SCREEN_HEIGHT (240)

void graphics_init(void);
void graphics_set_res(int width, int height);
void graphics_set_palette(const uint8_t i, const uint32_t color888);
void graphics_set_bgcolor(const uint32_t color888);
void graphics_shutdown(void);
void adjust_clk(void);
uint32_t get_frame_count(void);
