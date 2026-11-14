// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#include "VideoAutoDetect.h"

#ifdef ZX_VIDEO_AUTO_DETECT

#include "hardware/gpio.h"

#ifndef ZX_VGAHDMI_PIN_BASE
#define ZX_VGAHDMI_PIN_BASE 12
#endif
#ifndef ZX_VGAHDMI_PIN_COUNT
#define ZX_VGAHDMI_PIN_COUNT 8
#endif

// The TMDS clock pair on the shared GPIO12-19 block is CLK-/CLK+ =
// GPIO12/GPIO13 = bits 7/6 of the raw pin read (pin 12 = MSB).
#define ZX_VGAHDMI_CLK_PAIR_MASK 0xC0

static const char *_zxVideoAutoDetectName = 0;

// Diagnostics: raw pin state at probe time (pin 12 = bit 7 ... pin 19 =
// bit 0) and the number of pins probed. Read by the menu header.
uint8_t zxVideoAutoDetectPinsRaw = 0;
uint8_t zxVideoAutoDetectPinCount = ZX_VGAHDMI_PIN_COUNT;

bool zxVideoAutoDetect() {
  // All eight shared pins as inputs, weak pull-down. On the MURMULATOR2
  // board:
  //  - an HDMI/DVI sink terminates every TMDS line with 50R to 3.3V, so the
  //    TMDS + pins read HIGH against our pull-downs;
  //  - a VGA monitor terminates the RGB lines (12-17) with 75R to GND, and
  //    HSYNC/VSYNC (18/19) are monitor inputs that stay low against the
  //    pull-downs.
  for (uint p = ZX_VGAHDMI_PIN_BASE; p < ZX_VGAHDMI_PIN_BASE + ZX_VGAHDMI_PIN_COUNT; ++p) {
    gpio_init(p);
    gpio_set_dir(p, GPIO_IN);
    gpio_pull_down(p);
  }

  // Cheap busy-wait so the pull-downs settle and the sink termination
  // has time to show up (no timer needed this early).
  busy_wait_us_32(1000);

  // Store the raw 8-bit read of the shared pins for diagnostics:
  // pin 12 (base) in bit 7 ... pin 19 (base+7) in bit 0.
  uint8_t raw = 0;
  for (uint b = 0; b < ZX_VGAHDMI_PIN_COUNT; ++b) {
    raw = (uint8_t)(raw << 1);
    if (gpio_get(ZX_VGAHDMI_PIN_BASE + b)) raw |= 1u;
  }
  zxVideoAutoDetectPinsRaw = raw;

  // Empirical calibration (SpeccyP M2, user-measured 2026-11-13):
  //   pins raw = 0x00 -> VGA monitor or no sink
  //   pins raw = 0xC0 -> HDMI sink (ONLY the TMDS clock pair GPIO12+13
  //   reads HIGH; the D pairs terminate LOW unless actively driven)
  // Decision rule: HDMI iff BOTH clock-pair pins (GPIO12+13, mask 0xC0)
  // are HIGH; anything else is treated as a VGA monitor.
  const bool hdmi = (raw & ZX_VGAHDMI_CLK_PAIR_MASK) == ZX_VGAHDMI_CLK_PAIR_MASK;
  _zxVideoAutoDetectName = hdmi ? "HDMI" : "VGA";

  // Leave the pins as pulled-down inputs: whichever video driver is
  // selected re-configures them (dvi_init / scanvideo_setup).
  return hdmi;
}

const char *zxVideoAutoDetectName() {
  return _zxVideoAutoDetectName ? _zxVideoAutoDetectName : "?";
}

#endif // ZX_VIDEO_AUTO_DETECT
