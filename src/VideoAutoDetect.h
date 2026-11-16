// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#pragma once

#include <pico/stdlib.h>

// Boot-time VGA monitor vs HDMI sink auto-detect for the MURMULATOR2
// SpeccyP board, where the VGA connector and the DVI (HDMI) output share
// GPIO12-19:
//
//   DVI (murmulator2_hdmi_cfg):  TMDS pairs CLK=12/13, D0=14/15,
//                                D1=16/17, D2=18/19
//   VGA (scanvideo 222+VSYNC):   R=12,13  G=14,15  B=16,17,
//                                HSYNC=18, VSYNC=19
//
// The module is compiled in only for targets that define
// ZX_VIDEO_AUTO_DETECT; everywhere else the API is unavailable.

#ifdef ZX_VIDEO_AUTO_DETECT

// true if an HDMI/DVI sink was detected (termination pulls the TMDS
// pairs high), false if a VGA monitor is assumed.
bool zxVideoAutoDetect();

// "HDMI" or "VGA" after detection, "?" before it has run.
const char *zxVideoAutoDetectName();

// Raw 8-bit read of GPIO12..19 taken at probe time, pin 12 = bit 7
// (MSB) ... pin 19 = bit 0 (LSB). Exported for on-screen diagnostics
// while the detection rule is being calibrated.
extern uint8_t zxVideoAutoDetectPinsRaw;

// Number of shared pins probed (currently 8, GPIO12..19).
extern uint8_t zxVideoAutoDetectPinCount;

#endif
