// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
//
// BootSwap - reboot the ZX firmware into another firmware image from the SD.
//
// v0.2 "Boot Manager from the menu": the ZX firmware menu exposes a
// "-> Boot Manager" entry which stages /sys/bootman_rp2350.bin (BOOTMAN
// v0.2 PICOBIN image) into the first flash slot (0x10000000, slot 0) and
// watchdog-reboots straight into the boot manager browser.
//
// Safety gates (before ANY flash erase):
//  * platform gate  - only RP2350 (PICO_MCU == "rp2350-arm-s") builds may
//                     perform a swap; on RP2040 the file compiles to a
//                     stub whose bootSwapFromSd() just returns false.
//  * path gate      - the only accepted image is the exact boot-swap
//                     protocol name /sys/bootman_rp2350.bin.
//  * size gate      - image must be >0 and <= 1 MiB.
//  * PICOBIN gate   - the first 512 bytes must contain the PICOBIN marker
//                     0xFFFFDED3 followed by an IMAGE_TYPE item with value
//                     0x1021 (EXE|Secure|ARM|RP2350). A corrupt header or
//                     an RP2040 .bin (no PICOBIN header) is refused and
//                     flash is never touched.
//
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Fixed SD path used by the boot-swap menu entry. This exact name is part
// of the boot-swap protocol (see sd_bootman_20261102/README-testare.md).
#define BOOT_SWAP_PATH_BOOTMAN  "/sys/bootman_rp2350.bin"

// Largest accepted .bin image (firmware slot budget, same as BOOTMAN v0.2).
#define BOOT_SWAP_MAX_SIZE      (1024u * 1024u)

// Set to true the moment the system is quiesced for flashing (core1 stopped,
// DMA aborted). The main loop can observe it via bootSwapQuiesceHook.
extern volatile bool bootSwapQuiesced;

// Type for the optional main-loop stop hook.
typedef void (*BootSwapQuiesceHook)(void);

// Optional: the main loop may register a function that stops the render loop
// (e.g. raising a flag the video loop observes, then joining core1). If no
// hook is registered, BootSwap falls back to multicore_reset_core1().
// Returns the previously registered hook.
void bootSwapSetQuiesceHook(BootSwapQuiesceHook hook);

// Reads `path` from SD (FatFs, mounted by the platform's normal SD driver),
// applies the path / size / PICOBIN gates, reads a 4 KiB staging chunk
// into SRAM, then erases + programs the first flash slot (0x10000000)
// chunk-by-chunk and finally issues a watchdog reboot into the new image.
//
// On success this function never returns (the board reboots in the boot
// manager browser).
// Returns false (never touching flash!) when the file is missing, empty,
// oversized, has a bad PICOBIN header (corrupt or RP2040 image), fails a
// mid-flash SD read, or when the platform gate rejects the call.
bool bootSwapFromSd(const char *path);
