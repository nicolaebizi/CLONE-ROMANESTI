// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
//
// BootSwap implementation (see BootSwap.h). This is the ZX-firmware flavour;
// the C64 firmware carries its own adapted copy in c64/src/rp2350/bootSwap.c
// (duplicated on purpose - no cross dependencies between the two projects).
//
// Flash write strategy (XIP safety):
//  * All data used during the flash window lives in SRAM: the 4 KiB staging
//    chunk (pinned SRAM .bss, see `s_bootSwapChunk`) and the loop parameters.
//    No .rodata/.data/XIP reads happen inside the erase/program window.
//  * The erase + programming step is a single call into the pico-sdk
//    `hardware_flash` routines, which are themselves compiled out of the XIP
//    area (`__no_inline_not_in_flash_func` in SDK flash.c) and re-enable XIP
//    before returning - the same RAM-resident pattern proven by the BOOTMAN
//    v0.2 flasher in this repo (works without a custom linker script).
//  * Before the first erase the render core (core1) is stopped and every DMA
//    channel is aborted, so nothing can touch the flash controller during the
//    XIP-halt window. Interrupts are also disabled around every erase+program
//    pair (`save_and_disable_interrupts()`), matching the SDK contract.
//  * Chunk-by-chunk: read chunk into SRAM from SD first (XIP still live),
//    erase only that 4 KiB sector, program it, repeat. Never "erase all +
//    wait": a failed SD read therefore only risks the sectors already
//    written, recoverable via BOOTSEL.
//  * The image validation PICOBIN parse (marker 0xFFFFDED3 + IMAGE_TYPE item
//    0x1021, EXE|Secure|ARM|RP2350) is a re-derivation of the BOOTMAN v0.2
//    check in bootman/main.c (check_image(), lines ~316-442).
//
#include "BootSwap.h"

#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/flash.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/dma.h"
#include "hardware/watchdog.h"

// FatFs - the SD code (FatFsSpiInputStream, SdCardFatFsSpi ...) uses the
// same pimoroni-pico FatFs instance, mounted by the normal platform code.
#include "ff.h"

// PICOBIN chunk markers/constants (pico-sdk 2.x boot/picobin.h, same
// values as bootman/main.c).
#define PICOBIN_MARKER_START           0xffffded3u
#define PICOBIN_ITEM_1BS_IMAGE_TYPE    0x42
#define PICOBIN_IMAGE_TYPE_EXE_S_ARM_RP2350 0x1021

#if defined(PICO_RP2350)
#define BOOT_SWAP_PLATFORM_ALLOWED 1
#else
#define BOOT_SWAP_PLATFORM_ALLOWED 0
#endif

// ----------------------------------------------------------------------------
// Globals
// ----------------------------------------------------------------------------

volatile bool bootSwapQuiesced = false;

static BootSwapQuiesceHook s_bootSwapQuiesceHook = (BootSwapQuiesceHook)0;

// Optional: the main loop may register a function that stops the render loop
// (e.g. raising a flag the video loop observes, then joining core1). If no
// hook is registered, BootSwap falls back to multicore_reset_core1().
// Returns the previously registered hook.
// Defined on every platform (one single definition - kept out of the RP2350
// gate so the RP2040 build links it too; the stub never calls it).
void bootSwapSetQuiesceHook(BootSwapQuiesceHook hook) {
  s_bootSwapQuiesceHook = hook;
}

#if BOOT_SWAP_PLATFORM_ALLOWED

// 4 KiB staging chunk - pinned in SRAM (.bss is RAM-resident). Never touched
// while XIP is halted except by the ROM flash functions themselves.
// Defined inside the RP2350-gated section below (an RP2040 build never
// flashes, so it does not even need the buffer).
static uint8_t s_bootSwapChunk[FLASH_SECTOR_SIZE] __attribute__((aligned(4)));

// ----------------------------------------------------------------------------
// Platform / path gates
// ----------------------------------------------------------------------------

bool bootSwapPlatformIsRp2350(void) {
#if BOOT_SWAP_PLATFORM_ALLOWED
  // Compared datum is the PICO_MCU macro (PICO_MCU="${PICO_PLATFORM}").
  return 0 == strcmp(PICO_MCU, "rp2350-arm-s");
#else
  return false;
#endif
}

// Only the boot-swap protocol name is ever accepted; anything else (typos,
// other images, UF2s, unknown paths) is refused.
static bool bootSwapPathAllowed(const char *path) {
  return 0 == strcmp(path, BOOT_SWAP_PATH_BOOTMAN);
}

// ----------------------------------------------------------------------------
// PICOBIN validation gate (before any erase!)
// ----------------------------------------------------------------------------

typedef enum {
  BOOT_SWAP_HDR_OK = 0,
  BOOT_SWAP_HDR_NO_HEADER,   // no PICOBIN marker (RP2040 image or bad bin)
  BOOT_SWAP_HDR_CHUNK_BAD,   // marker found but IMAGE_TYPE item is not
                             // EXE|Secure|ARM|RP2350
} boot_swap_hdr_check_t;

// Reads the first 512 bytes and parses the PICOBIN header. Either a valid
// EXE|Secure|ARM|RP2350 image block (as emitted by pico-sdk 2.x pico2) is
// found, or the image is refused. An RP2040 .bin has no PICOBIN header, so
// it can never pass this check. (Logic replicated from bootman/main.c
// check_image(), lines ~316-442, with new names.)
static boot_swap_hdr_check_t bootSwapCheckPicobin(FIL *file) {
  static uint8_t head[512]; // SRAM only, never touched during flash window
  UINT rd = 0;
  if (f_lseek(file, 0) != FR_OK) return BOOT_SWAP_HDR_NO_HEADER;
  if (f_read(file, head, sizeof(head), &rd) != FR_OK || rd != sizeof(head))
    return BOOT_SWAP_HDR_NO_HEADER;
  f_lseek(file, 0);

  // blank image guard (all 0xFF / erased sector)
  bool blank = true;
  for (uint32_t i = 0; i < 64; i++) if (head[i] != 0xFF) { blank = false; break; }
  if (blank) return BOOT_SWAP_HDR_NO_HEADER;

  uint32_t marker_off = 0xffffffffu;
  for (uint32_t o = 0; o + 4 <= sizeof(head); o += 4) {
    uint32_t w;
    memcpy(&w, head + o, 4);
    if (w == PICOBIN_MARKER_START) {
      marker_off = o;
      break;
    }
  }
  if (marker_off == 0xffffffffu) return BOOT_SWAP_HDR_NO_HEADER;

  // Layout (verified on the builds in this repo):
  //   marker_off+0 : PICOBIN_MARKER_START (4 bytes, 0xFFFFDED3)
  //   marker_off+4 : first item: {tag 0x42 IMAGE_TYPE, count 0x01,
  //                  hword value 0x1021 = EXE|Secure|ARM|RP2350}
  //   marker_off+8 : LAST item tag (0xFF), then the END marker follows.
  if (head[marker_off + 4] != PICOBIN_ITEM_1BS_IMAGE_TYPE)
    return BOOT_SWAP_HDR_CHUNK_BAD;
  if (head[marker_off + 5] != 0x01) // 1 word payload
    return BOOT_SWAP_HDR_CHUNK_BAD;
  uint16_t v;
  memcpy(&v, head + marker_off + 6, 2);
  if (v != PICOBIN_IMAGE_TYPE_EXE_S_ARM_RP2350)
    return BOOT_SWAP_HDR_CHUNK_BAD;
  return BOOT_SWAP_HDR_OK;
}

// ----------------------------------------------------------------------------
// Quiesce: stop render core + abort DMA (mute audio)
// ----------------------------------------------------------------------------

static void bootSwapAbortDma(void) {
  // Abort every DMA channel (video scanout, audio (I2S/PWM) feeds, ...). This
  // both stops the audio (mute for flash) and guarantees no DMA transfer can
  // touch the flash-mapped buffers while XIP is halted.
  for (uint32_t ch = 0; ch < NUM_DMA_CHANNELS; ++ch) {
    dma_channel_abort((uint)ch);
  }
  // Belt & braces: quiesce the video/audio DMA IRQs as well (RP2350 has the
  // four DMA_IRQ_0..3 lines).
  for (uint32_t i = 0; i < 4; ++i) {
    irq_set_enabled(DMA_IRQ_0 + i, false);
  }
}

// The current ZX render loop (ZxScanlineVgaRenderLoop / ZxDviRenderLoop)
// runs inside core1 (vga/main.cpp: multicore_launch_core1(core1_main)) and
// has no stop hook, so the pragmatic fallback is to reset core1 via
// multicore_reset_core1() (core1 parks in the boot ROM, far away from XIP).
// A main-loop hook can still be registered via bootSwapSetQuiesceHook() for
// a cooperative stop on future firmware versions.
static void bootSwapQuiesce(void) {
  bootSwapQuiesced = true;

  if (s_bootSwapQuiesceHook) {
    s_bootSwapQuiesceHook();
  }
  else {
    multicore_reset_core1();
  }

  bootSwapAbortDma();
}

// ----------------------------------------------------------------------------
// Flash staging
// ----------------------------------------------------------------------------

// Erase + program one 4 KiB chunk. The SDK functions are RAM-resident and
// restore XIP before returning to the (flash-resident) caller.
static void __no_inline_not_in_flash_func(bootSwapEraseProgramChunk)(
  uint32_t offset,
  const uint8_t *chunk,
  uint32_t len
) {
  const uint32_t ints = save_and_disable_interrupts();
  flash_range_erase(offset, FLASH_SECTOR_SIZE);
  flash_range_program(offset, chunk, len); // len padded to a 256-byte multiple
  restore_interrupts(ints);
}

// Pad `len` bytes in the chunk buffer up to the next 256-byte flash program
// granularity with 0xFF.
static uint32_t bootSwapPadProgramLen(uint32_t len) {
  len = (len + 255u) & ~255u;
  if (len > FLASH_SECTOR_SIZE) len = FLASH_SECTOR_SIZE;
  memset(s_bootSwapChunk + len, 0xFF, FLASH_SECTOR_SIZE - len);
  return len;
}

// ----------------------------------------------------------------------------
// Main entry
// ----------------------------------------------------------------------------

bool bootSwapFromSd(const char *path) {
  // Platform gate: refused any build / call combination that is not an
  // RP2350 firmware (compile-time on RP2040, runtime compare of PICO_MCU).
  if (!bootSwapPlatformIsRp2350()) {
    printf("BootSwap: refused '%s': platform %s can not boot-swap\n",
      path, PICO_MCU);
    return false;
  }

  // Path gate: only the protocol name is accepted.
  if (!bootSwapPathAllowed(path)) {
    printf("BootSwap: refused '%s': not a boot-swap image name\n", path);
    return false;
  }

  // (a) open the file like the rest of the SD code (same FatFs instance).
  FIL file;
  FRESULT fr = f_open(&file, path, FA_READ);
  if (fr != FR_OK) {
    printf("BootSwap: '%s' f_open failed (%d)\n", path, (int)fr);
    return false;
  }

  const uint32_t size = (uint32_t)f_size(&file);
  if (size == 0 || size > BOOT_SWAP_MAX_SIZE) {
    printf("BootSwap: refused '%s': %lu bytes (budget 1..%u bytes)\n",
      path, (unsigned long)size, (unsigned)BOOT_SWAP_MAX_SIZE);
    f_close(&file);
    return false;
  }

  // (b) PICOBIN gate BEFORE any erase: refuse a corrupt header / RP2040 bin.
  const boot_swap_hdr_check_t hc = bootSwapCheckPicobin(&file);
  if (hc != BOOT_SWAP_HDR_OK) {
    printf("BootSwap: refused '%s': %s\n", path,
      hc == BOOT_SWAP_HDR_CHUNK_BAD
        ? "bad PICOBIN IMAGE_TYPE item (not EXE|Secure|ARM|RP2350)"
        : "no PICOBIN header (RP2040 image or corrupt bin)");
    f_close(&file);
    return false;
  }

  // (c) quiesce: stop the render core and all DMA before touching flash.
  printf("BootSwap: quiesce (stop render core, abort DMA)\n");
  bootSwapQuiesce();

  // (d,e) stage chunk-by-chunk: read 4 KiB into SRAM, erase + program it.
  const uint32_t total = size;
  uint32_t done = 0;
  while (done < total) {
    uint32_t n = total - done;
    if (n > FLASH_SECTOR_SIZE) n = FLASH_SECTOR_SIZE;

    UINT rd = 0;
    if (f_read(&file, s_bootSwapChunk, n, &rd) != FR_OK || rd != n) {
      // Mid-flash SD failure: some sectors may already be overwritten, so
      // booting now would risk a corrupt image. Do NOT reboot - hold here
      // and let the user recover via BOOTSEL (documented in
      // sd_bootman_20261102/README-testare.md).
      printf("BootSwap: SD read failed at %lu/%lu - HOLDING (BOOTSEL to recover)\n",
        (unsigned long)done, (unsigned long)total);
      while (true) tight_loop_contents();
    }

    const uint32_t len = bootSwapPadProgramLen(n);
    bootSwapEraseProgramChunk(done, s_bootSwapChunk, len);
    done += n;
    if (0 == (done & (FLASH_SECTOR_SIZE * 16u - 1u))) {
      printf("BootSwap: %lu/%lu bytes\n", (unsigned long)done, (unsigned long)total);
    }
  }

  f_close(&file);

  // (f) reboot into the freshly flashed image (the boot manager browser).
  printf("BootSwap: flash ok (%lu bytes), watchdog reboot into BOOTMAN\n",
    (unsigned long)total);
  watchdog_reboot(0, 0, 100); // pc=0/sp=0 -> normal reboot through the bootrom
  while (true) tight_loop_contents();
  __builtin_unreachable();
}

#else // !RP2350: empty stub so every target compiles the common source set

bool bootSwapFromSd(const char *path) {
  (void)path;
  return false; // gate: boot swap only exists on RP2350
}

#endif
