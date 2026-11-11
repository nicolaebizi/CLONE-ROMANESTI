/*
 * bootSwap.c - FRANK C64 "Boot Manager" flash-from-SD swap (RP2350).
 *
 * Duplicated on purpose from the ZX firmware BootSwap implementation
 * (pico-zxspectrum/src/BootSwap.cpp): adapted to the C64 platform quirks.
 *
 * XIP safety: the erase/program window uses the same sanctioned pattern as
 * the project's own Cartridge.cpp flash writer (flash_block()):
 *  - the 4 KiB staging chunk lives in SRAM (.bss),
 *  - chip erase+program are the pico-sdk `hardware_flash` routines compiled
 *    out of the XIP area (__no_inline_not_in_flash_func in SDK flash.c),
 *  - core1 (declared victim via multicore_lockout_victim_init(), see
 *    main_rp2350.c:100) is frozen by multicore_lockout_start_blocking(),
 *    then all interrupts are masked by save_and_disable_interrupts(),
 *    exactly as in c64/src/Cartridge.cpp:51-63.
 * The chunk staging (SD read into SRAM) happens while XIP is still live;
 * each erase+program re-enables XIP on return, so the next SD read is safe.
 *
 * PICOBIN gate (before ANY erase): the first 512 bytes must contain the
 * marker 0xFFFFDED3 followed by an IMAGE_TYPE item 0x1021
 * (EXE|Secure|ARM|RP2350) - logic re-derived from bootman/main.c
 * check_image() (lines ~316-442). Corrupt headers and RP2040 .bin images
 * are refused and flash is never touched.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/watchdog.h"

#include "fatfs/ff.h"

#include "bootSwap.h"

// PICOBIN chunk markers/constants (pico-sdk 2.x boot/picobin.h, same
// values as bootman/main.c).
#define PICOBIN_MARKER_START                0xffffded3u
#define PICOBIN_ITEM_1BS_IMAGE_TYPE         0x42
#define PICOBIN_IMAGE_TYPE_EXE_S_ARM_RP2350 0x1021

// The host board is RP2350-only (C64_rp2350 target compiles on pico2+) but
// keep a compile-time gate mirroring bootman/main.c:51 for safety.
#if !PICO_RP2350
#error "bootSwap.c: RP2350 only (this whole firmware port is RP2350)"
#endif

// 4 KiB staging chunk - SRAM (.bss), never touched during the XIP halt
// window except by the ROM flash functions themselves.
static uint8_t s_bootSwapChunk[FLASH_SECTOR_SIZE] __attribute__((aligned(4)));

static volatile bool s_bootSwapRunning = false;

//=============================================================================
// PICOBIN header validation (no erase before this passes)
//=============================================================================

typedef enum {
    BOOT_SWAP_HDR_OK = 0,
    BOOT_SWAP_HDR_NO_HEADER,  // no PICOBIN marker (RP2040 image or bad bin)
    BOOT_SWAP_HDR_CHUNK_BAD,  // marker found but IMAGE_TYPE is not EXE|Secure|ARM|RP2350
} boot_swap_hdr_check_t;

// Logic replicated from bootman/main.c check_image() (new names, C).
static boot_swap_hdr_check_t boot_swap_check_picobin(FIL *file) {
    static uint8_t head[512];            // SRAM only
    UINT rd = 0;
    if (f_lseek(file, 0) != FR_OK) return BOOT_SWAP_HDR_NO_HEADER;
    if (f_read(file, head, sizeof(head), &rd) != FR_OK || rd != sizeof(head))
        return BOOT_SWAP_HDR_NO_HEADER;
    f_lseek(file, 0);

    bool blank = true;                   // erased-sector guard
    for (uint32_t i = 0; i < 64; i++)
        if (head[i] != 0xFF) { blank = false; break; }
    if (blank) return BOOT_SWAP_HDR_NO_HEADER;

    uint32_t marker_off = 0xffffffffu;   // find marker, word aligned
    for (uint32_t o = 0; o + 4 <= sizeof(head); o += 4) {
        uint32_t w;
        memcpy(&w, head + o, 4);
        if (w == PICOBIN_MARKER_START) { marker_off = o; break; }
    }
    if (marker_off == 0xffffffffu) return BOOT_SWAP_HDR_NO_HEADER;

    // marker_off+4: {tag 0x42 IMAGE_TYPE, count 0x01, hword 0x1021}
    if (head[marker_off + 4] != PICOBIN_ITEM_1BS_IMAGE_TYPE)
        return BOOT_SWAP_HDR_CHUNK_BAD;
    if (head[marker_off + 5] != 0x01)    // 1 word payload
        return BOOT_SWAP_HDR_CHUNK_BAD;
    uint16_t v;
    memcpy(&v, head + marker_off + 6, 2);
    if (v != PICOBIN_IMAGE_TYPE_EXE_S_ARM_RP2350)
        return BOOT_SWAP_HDR_CHUNK_BAD;
    return BOOT_SWAP_HDR_OK;
}

//=============================================================================
// Quiesce + flash staging
//=============================================================================

// Same gates as c64/src/Cartridge.cpp:51-63 flash_block(): core1 victim
// lockout (VIDEO_HDMI core1 video task is a registered victim through
// multicore_lockout_victim_init(), main_rp2350.c:100) + all IRQs masked.
static void __no_inline_not_in_flash_func(boot_swap_erase_program_chunk)(
        uint32_t offset, const uint8_t *chunk, uint32_t len) {
    // len: 256-byte-rounded program length (boot_swap_pad_program_len());
    // the erase is always a full 4 KiB sector, program only what is staged.
#ifdef VIDEO_HDMI
    multicore_lockout_start_blocking();
#endif
    const uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
    flash_range_program(offset, chunk, len);
    restore_interrupts(ints);
#ifdef VIDEO_HDMI
    multicore_lockout_end_blocking();
#endif
}

// Pad `len` bytes up to the 256-byte flash program granularity with 0xFF.
static uint32_t boot_swap_pad_program_len(uint32_t len) {
    len = (len + 255u) & ~255u;
    if (len > FLASH_SECTOR_SIZE) len = FLASH_SECTOR_SIZE;
    memset(s_bootSwapChunk + len, 0xFF, FLASH_SECTOR_SIZE - len);
    return len;
}

//=============================================================================
// Main entry
//=============================================================================

int boot_swap_from_sd(void) {
    if (s_bootSwapRunning) return 0; // re-entrancy guard (should not happen)
    s_bootSwapRunning = true;

    printf("BOOTSWAP: '%s' requested\n", BOOT_SWAP_PATH_BOOTMAN);

    FIL file;
    FRESULT fr = f_open(&file, BOOT_SWAP_PATH_BOOTMAN, FA_READ);
    if (fr != FR_OK) {
        printf("BOOTSWAP: refused: f_open failed (%d)\n", (int)fr);
        s_bootSwapRunning = false;
        return 0;
    }

    const uint32_t size = (uint32_t)f_size(&file);
    if (size == 0 || size > BOOT_SWAP_MAX_SIZE) {
        printf("BOOTSWAP: refused: %lu bytes (budget 1..%u bytes)\n",
               (unsigned long)size, (unsigned)BOOT_SWAP_MAX_SIZE);
        f_close(&file);
        s_bootSwapRunning = false;
        return 0;
    }

    // PICOBIN gate BEFORE any erase: refuse corrupt header / RP2040 image.
    const boot_swap_hdr_check_t hc = boot_swap_check_picobin(&file);
    if (hc != BOOT_SWAP_HDR_OK) {
        printf("BOOTSWAP: refused: %s\n",
               (hc == BOOT_SWAP_HDR_CHUNK_BAD)
                   ? "bad PICOBIN IMAGE_TYPE item (not EXE|Secure|ARM|RP2350)"
                   : "no PICOBIN header (RP2040 image or corrupt bin)");
        f_close(&file);
        s_bootSwapRunning = false;
        return 0;
    }

    // Mute the SID I2S feed for the flash window (DMA aborted below) so no
    // DMA transfer can touch flash-mapped buffers while XIP is halted.
    for (uint32_t ch = 0; ch < NUM_DMA_CHANNELS; ++ch) {
        dma_channel_abort((uint)ch);
    }
    for (uint32_t i = 0; i < 4; ++i) { // RP2350 DMA_IRQ_0..3
        irq_set_enabled(DMA_IRQ_0 + i, false);
    }

    // Chunk-by-chunk staging: SD read into SRAM (XIP live), then erase +
    // program that 4 KiB sector (IRQs off, core1 locked out), repeat.
    const uint32_t total = size;
    uint32_t done = 0;
    while (done < total) {
        uint32_t n = total - done;
        if (n > FLASH_SECTOR_SIZE) n = FLASH_SECTOR_SIZE;

        UINT rd = 0;
        if (f_read(&file, s_bootSwapChunk, n, &rd) != FR_OK || rd != n) {
            // Mid-flash SD failure: do NOT reboot, keep the board reachable
            // by BOOTSEL (same hold behaviour as BOOTMAN v0.2).
            printf("BOOTSWAP: SD read failed at %lu/%lu - HOLDING (BOOTSEL to recover)\n",
                   (unsigned long)done, (unsigned long)total);
            while (true) __wfi();
        }

        boot_swap_pad_program_len(n);
        boot_swap_erase_program_chunk(done, s_bootSwapChunk, n);
        done += n;
        if (0 == (done & (FLASH_SECTOR_SIZE * 16u - 1u))) {
            printf("BOOTSWAP: %lu/%lu bytes\n",
                   (unsigned long)done, (unsigned long)total);
        }
    }

    f_close(&file);

    // Reboot straight into the freshly flashed BOOTMAN browser.
    printf("BOOTSWAP: flash ok (%lu bytes), watchdog reboot into BOOTMAN\n",
           (unsigned long)total);
    watchdog_reboot(0, 0, 100); // pc=0/sp=0 -> normal reboot through bootrom
    while (true) __wfi();
    __builtin_unreachable();
}
