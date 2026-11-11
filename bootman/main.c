/*
 *  main.c - BOOTMAN: standalone boot manager for the Murmulator M1 (RP2350)
 *
 *  Small permanent firmware flashed on the board. On EVERY boot it:
 *   1. brings up the VGA video driver (320x240, 8bpp, adapted FRANK C64 driver)
 *   2. mounts the SD card and lists /pico2/*.bin AND /pico2/*.uf2
 *   3. the user picks a file (PS/2 arrows / NES pad D-pad) and confirms with
 *      ENTER (PS/2) or A/START (NES pad). No timeout - explicit confirm only.
 *   4. verifies the image before touching flash:
 *      - .bin: PICOBIN header check (EXE|Secure|ARM|RP2350 - the exact value
 *        measured on the verified pico-sdk 2.x pico2 images built by this
 *        repo; RP2040 or malformed images are refused before any erase)
 *      - .uf2: streaming parse of this repo's forked UF2 layout (block 512B,
 *        familyID @28, addr @12, payload 256B @32). Payload blocks must be
 *        addr 0x10000000 step 256 sequential; image-table blocks
 *        (addr 0x10FFFF00, flags 0xA000, family 0xE48BFF57) are skipped.
 *        Only RP2350 family 0xE48BFF59 is accepted - RP2040 (0xE48BFF56)
 *        or any other family is REFUSED. Derived image size must stay
 *        within the 1 MiB budget.
 *   5. quiesces video/DMA, programs the image chunk-by-chunk (4 KiB) into
 *      flash slot 0 at 0x10000000 (for .uf2 the file is re-read from the SD
 *      at flash-time and payload is written sector-by-sector, no full file
 *      buffer in RAM), then reboots into the chosen firmware.
 *      v0.3.2: rewinds the .bin file after the header check (the exact
 *      f_lseek(file, 0) mirror of c64/src/rp2350/bootSwap.c:78 - without it
 *      every chunk was read 512-byte-shifted and the last read came up
 *      short at EOF), drops to an SDK-safe 125 MHz flash clock, verifies
 *      each programmed range via XIP readback, and reports the flash phase
 *      on the board LED (video is already off): 1 long blip at phase
 *      start, a short heartbeat every 64 KiB, 10 Hz forever on a mid-loop
 *      SD read failure, 3 by-150 ms repeating on a verify failure, and
 *      2 by-150 ms on success right before watchdog_reboot.
 *
 *  Recovery: if anything goes wrong mid-flash, hold the power off and press
 *  BOOTSEL while plugging USB back in - BOOTSEL mode is built into the RP2350
 *  boot ROM and does NOT depend on slot 0 being valid.
 *
 *  BOOTMAN - standalone boot manager for Murmulator RP2350
 *  Standalone C; no dependencies on the ZX/c64 firmware code.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <strings.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "hardware/flash.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/dma.h"
#include "hardware/structs/sysinfo.h"
#if PICO_RP2350
#include "hardware/structs/qmi.h"
#endif

// v0.2 platform gate: compiles on RP2350 (pico2: VGA or HDMI) AND on
// RP2040 (pico: VGA only). Per-platform branches below - no #error.

// board layout + text grid (both video headers also include this)
#include "board_config.h"

#if defined(VIDEO_HDMI)
#include "HDMI.h"
#include "pico/multicore.h"
#else
#include "../drivers/vga_bootman.h"
#endif

// platform drivers (vendored copies from frank-c64, adapted headers)
#include "ps2kbd/ps2kbd_wrapper.h"
#include "nespad/nespad.h"

// SD + file system
#include "ff.h"

// bootman font (identical byte layout as FRANK C64 src/MenuFont.h)
#include "MenuFont.h"

// debug log macro (ENABLE_DEBUG_LOGS=0 in this build)
#include "debug_log.h"

// HDMI driver reads this for the on-screen error LED overlay (unused here,
// defined to satisfy the vendored driver's extern).
uint32_t __led_state = 0;

//=============================================================================
// Constants
//=============================================================================

#define BOOTMAN_VERSION "v0.3.3"

// Per-platform manager semantics (v0.2):
//   RP2350 (pico2): browser /pico2, .bin PICOBIN + .uf2 family 0xE48BFF59
//   RP2040 (pico) : browser /pico1, .uf2 family 0xE48BFF56 ONLY (".bin refused")
#if PICO_RP2350
#if defined(VIDEO_HDMI)
#define BOOTMAN_BUILD_TAG "v0.3.3-HDMI"
#else
#define BOOTMAN_BUILD_TAG "v0.3.3"
#endif
#define IMAGE_DIR       "/pico2"
// flash region: the whole 4 MiB flash of the M1 (slot 0 prefix)
#define FLASH_SIZE_BYTES (4u * 1024u * 1024u)
#else // RP2040
#define BOOTMAN_BUILD_TAG "v0.3.3-RP2040"
#define IMAGE_DIR       "/pico1"
// flash region: the whole 2 MiB flash of the pico board
#define FLASH_SIZE_BYTES (2u * 1024u * 1024u)
#endif

#define IMAGE_EXT       ".bin"
#define IMAGE_EXT_UF2   ".uf2"

// hard limit: 1 MiB budget (and never beyond the board flash)
#define MAX_IMAGE_SIZE  (1024u * 1024u)

// PICOBIN chunk markers/constants (pico-sdk 2.3.1 boot/picobin.h)
#define PICOBIN_BLOCK_MARKER_START 0xffffded3u
#define PICOBIN_BLOCK_ITEM_1BS_IMAGE_TYPE 0x42

// IMAGE_TYPE item value for the ROM-bootable default pico2 image:
//   EXE | Secure | ARM | RP2350  ==  0x1021
// (measured at offset 0x138 first-item on verified m1p2-frank-c64-VGA and
//  ZX-MURMULATOR_VGA bins built by this repo with pico-sdk 2.3.1.)
#define PICOBIN_IMAGE_TYPE_EXE_S_ARM_RP2350 0x1021

//-----------------------------------------------------------------------------
// UF2 (repo fork layout, offsets verified by xxd on every *.uf2 built here:
// m1p2-frank-c64-VGA-...-v1.08, ZX-MURMULATOR2_*, bootman-murmulator-...):
//   0  magic0 0x0A324655
//   4  magic1 0x9E5D5157
//   8  flags (payload: 0x2000; image-table block: 0xA000)
//  12  target address   (payload: 0x10000000+i*256; image table: 0x10FFFF00)
//  16  payload size (256)
//  20  block sequence number
//  24  last block index of the file
//  28  family ID        (payload: RP2350=0xE48BFF59; image table: 0xE48BFF57)
//  32  payload, 256 bytes
// 508  magic end 0x0AB16F30
#define UF2_BLOCK_SIZE       512u
#define UF2_MAGIC_START0     0x0A324655u
#define UF2_MAGIC_START1     0x9E5D5157u
#define UF2_MAGIC_END        0x0AB16F30u
#define UF2_FLAG_FAMILY      0x2000u
#define UF2_ADDR_FIRST       0x10000000u
#define UF2_ADDR_IMAGE_TABLE 0x10FFFF00u
#define UF2_FAMILY_RP2350    0xE48BFF59u
#define UF2_FAMILY_RP2040    0xE48BFF56u
#define UF2_PAYLOAD          256u

// the UF2 family this manager accepts (target chip only, others refused)
#if PICO_RP2350
#define UF2_FAMILY_TARGET UF2_FAMILY_RP2350
#else
#define UF2_FAMILY_TARGET UF2_FAMILY_RP2040
#endif

#define MAX_FILES   64
#define MAX_NAME    96
#define LIST_TOP    3                // first text row of the file list
#define LIST_ROWS   (BOOTMAN_ROWS - LIST_TOP - 2)

// palette indices
#define C_BLACK  0
#define C_WHITE  1
#define C_GREY   2
#define C_GREEN  3
#define C_YELLOW 4
#define C_RED    5

//=============================================================================
// Frame buffer + text grid
//=============================================================================

static uint8_t g_fb[BOOTMAN_FB_WIDTH * BOOTMAN_FB_HEIGHT];

uint8_t* graphics_get_buffer_line(int y) {
    return g_fb + y * BOOTMAN_FB_WIDTH;
}

static void fb_clear(void) {
    memset(g_fb, 0, sizeof(g_fb));
}

static void fb_rect(int x, int y, int w, int h, uint8_t color) {
    for (int r = 0; r < h; r++) {
        uint8_t *row = g_fb + (y + r) * BOOTMAN_FB_WIDTH + x;
        for (int c = 0; c < w; c++) row[c] = color;
    }
}

// 8px-tall font, row-encoding, MSB = leftmost pixel, advance width from
// menu_char_width[] (same conventions as FRANK C64 Display::draw_string).
static void fb_draw_text(int x, int y, const char *str, uint8_t color) {
    unsigned char c;
    while ((c = *str++) != 0) {
        if (c >= 0x80) c = 0x7f; // replacement
        uint8_t w = 8;
        if (c < sizeof(menu_char_width)) w = menu_char_width[c];
        const uint8_t *q = menu_font + c * 8;
        if (x + (int)w > BOOTMAN_FB_WIDTH) break;
        uint8_t *p = g_fb + y * BOOTMAN_FB_WIDTH + x;
        for (int r = 0; r < 8; r++) {
            uint8_t v = *q++;
            for (int cx = 0; cx < w; cx++) {
                if (v & 0x80) p[cx] = color;
                v <<= 1;
            }
            p += BOOTMAN_FB_WIDTH;
        }
        x += w;
    }
}

static void fb_text(int row, const char *str, uint8_t fg) {
    fb_rect(0, row * 8, BOOTMAN_FB_WIDTH, 8, C_BLACK);
    fb_draw_text(2, row * 8, str, fg);
}

static void fb_hbar(int row, const char *text, uint8_t bg, uint8_t fg) {
    fb_rect(0, row * 8, BOOTMAN_FB_WIDTH, 8, bg);
    fb_draw_text(2, row * 8, text, fg);
}

//=============================================================================
// File list
//=============================================================================

typedef struct {
    char name[MAX_NAME];
    uint32_t size;
    bool is_uf2;     // .uf2 fork-layout image; .bin PICOBIN image otherwise
} file_entry_t;

static file_entry_t g_files[MAX_FILES];
static int g_file_count = 0;
static int g_selected = 0;
static int g_scroll_top = 0;

static FATFS g_fatfs;

static bool file_ext_is(const char *name, const char *ext) {
    size_t n = strlen(name);
    const size_t e = strlen(ext);
    if (n <= e) return false;
    return strcasecmp(name + n - e, ext) == 0;
}

static void scan_image_dir(void) {
    g_file_count = 0;
    DIR dir;
    FILINFO fi;
    FRESULT fr = f_opendir(&dir, IMAGE_DIR);
    if (fr != FR_OK) return;
    while (g_file_count < MAX_FILES) {
        fr = f_readdir(&dir, &fi);
        if (fr != FR_OK || fi.fname[0] == 0) break;
        if (fi.fattrib & AM_DIR) continue;
        const bool uf2 = file_ext_is(fi.fname, IMAGE_EXT_UF2);
#if !PICO_RP2350
        // RP2040 manager lists .uf2 (family 0xE48BFF56) ONLY; raw .bin files
        // (boot2-prefixed, no PICOBIN header) are never offered for flash.
        if (!uf2) continue;
#endif
        if (!uf2 && !file_ext_is(fi.fname, IMAGE_EXT)) continue;
        strncpy(g_files[g_file_count].name, fi.fname, MAX_NAME - 1);
        g_files[g_file_count].name[MAX_NAME - 1] = '\0';
        g_files[g_file_count].size = (uint32_t)fi.fsize;
        g_files[g_file_count].is_uf2 = uf2;
        g_file_count++;
    }
    f_closedir(&dir);
    if (g_selected >= g_file_count) g_selected = 0;
    if (g_scroll_top >= g_file_count) g_scroll_top = 0;
}

//=============================================================================
// Screen UI
//=============================================================================

static void draw_browser(const char *chip_label, bool sd_ok) {
    fb_clear();
    char line[128];
    snprintf(line, sizeof(line), "BOOTMAN %s  Murmulator M1  %s",
             BOOTMAN_BUILD_TAG, chip_label);
    fb_hbar(0, line, C_WHITE, C_BLACK);

    if (g_file_count > 0) {
#if PICO_RP2350
        snprintf(line, sizeof(line), "Pick firmware from %s (%d files)", IMAGE_DIR, g_file_count);
#else
        snprintf(line, sizeof(line), "RP2040: use .uf2 - pick from %s (%d files)",
                 IMAGE_DIR, g_file_count);
#endif
        fb_text(1, line, C_GREY);
        for (int r = 0; r < LIST_ROWS; r++) {
            int idx = g_scroll_top + r;
            if (idx >= g_file_count) break;
            snprintf(line, sizeof(line), "> %.44s %6lu KiB %-3s",
                     g_files[idx].name,
                     (unsigned long)(g_files[idx].size / 1024),
                     g_files[idx].is_uf2 ? "UF2" : "BIN");
            uint8_t fg = (idx == g_selected) ? C_YELLOW : C_WHITE;
            if (idx == g_selected) {
                fb_text(LIST_TOP + r, line, fg);
                fb_rect(0, (LIST_TOP + r) * 8, 2, 8, C_YELLOW);
            } else {
                fb_text(LIST_TOP + r, line + 2, fg); // no arrow on unselected
            }
        }
    } else if (!sd_ok) {
        fb_text(1, "No SD card / FAT mount failed.", C_RED);
#if PICO_RP2350
        fb_text(2, "Insert SD with /pico2/*.bin/*.uf2 and reboot.", C_RED);
#else
        fb_text(2, "Insert SD with /pico1/*.uf2 and reboot.", C_RED);
#endif
    } else {
#if PICO_RP2350
        fb_text(1, "No *.bin / *.uf2 files in " IMAGE_DIR ".", C_RED);
#else
        fb_text(1, "RP2040: use .uf2 only.", C_RED);
        fb_text(2, "Add *.uf2 (family 0xE48BFF56) to " IMAGE_DIR ".", C_RED);
#endif
    }
#if PICO_RP2350
    fb_text(BOOTMAN_ROWS - 2, "UP/DOWN move - ENTER=flash - ESC=cancel", C_GREY);
#else
    fb_text(BOOTMAN_ROWS - 2, "RP2040: flash using .uf2 only - ENTER=flash", C_GREY);
#endif
    fb_text(BOOTMAN_ROWS - 1, "Recovery: hold BOOTSEL while replugging USB", C_RED);
}

static void draw_confirm(int idx) {
    fb_clear();
    char line[128];
    fb_hbar(0, "FLASH FIRMWARE? (BIN/UF2)", C_WHITE, C_BLACK);
    snprintf(line, sizeof(line), "file : %.52s", g_files[idx].name);
    fb_text(2, line, C_WHITE);
    snprintf(line, sizeof(line), "size : %lu bytes (%lu KiB) [%s]",
             (unsigned long)g_files[idx].size,
             (unsigned long)(g_files[idx].size / 1024),
             g_files[idx].is_uf2 ? "UF2" : "BIN");
    fb_text(3, line, C_WHITE);
    snprintf(line, sizeof(line), "slot : 0x10000000 (flash slot 0) + RESET");
    fb_text(4, line, C_RED);
    fb_text(5, "The boot manager erases itself and", C_RED);
    fb_text(6, "is replaced by the selected image.", C_RED);
    fb_text(BOOTMAN_ROWS - 2, "ENTER (PS/2) or A (pad) = START FLASH", C_GREEN);
    fb_text(BOOTMAN_ROWS - 1, "ESC / B = cancel (no timeout)", C_GREY);
}

//=============================================================================
// Input (PS/2 + NES pad)
//=============================================================================

// Long-press auto-repeat is intentionally omitted: selection is driven by
// new-press edges only.
static bool ps2_key_take(uint8_t *code) {
    int pressed;
    unsigned char k;
    while (ps2kbd_get_key(&pressed, &k)) {
        if (pressed) {
            *code = (uint8_t)k;
            return true;
        }
    }
    return false;
}

//=============================================================================
// Clock / flash timing
//=============================================================================
// RP2350: same approach as FRANK C64 main_rp2350.c (QMI flash timings +
// over-voltage for >252 MHz).
// RP2040: conservative clocks only - the SDK boot2/QSPI setup is left at
// defaults, no QMI (RP2350-only peripheral) and no voltage-limit override.

#if PICO_RP2350
static void __no_inline_not_in_flash_func(set_flash_timings)(int cpu_mhz) {
    const int clock_hz = cpu_mhz * 1000000;
    const int max_flash_freq = FLASH_MAX_FREQ_MHZ * 1000000;

    int divisor = (clock_hz + max_flash_freq - (max_flash_freq >> 4) - 1) / max_flash_freq;
    if (divisor == 1 && clock_hz >= 166000000) {
        divisor = 2;
    }
    int rxdelay = divisor;
    if (clock_hz / divisor > 100000000 && clock_hz >= 166000000) {
        rxdelay += 1;
    }
    qmi_hw->m[0].timing = 0x60007000 |
                          ((uint32_t)rxdelay << QMI_M0_TIMING_RXDELAY_LSB) |
                          ((uint32_t)divisor << QMI_M0_TIMING_CLKDIV_LSB);
}
#endif // PICO_RP2350

static void init_clocks(void) {
#if PICO_RP2350
    // Overclock before stdio (matching the murmulator approach)
    vreg_disable_voltage_limit();
    sleep_ms(10);
#if CPU_CLOCK_MHZ > 252
    vreg_set_voltage(CPU_VOLTAGE);
    set_flash_timings(CPU_CLOCK_MHZ);
#endif
    sleep_ms(33);
    set_sys_clock_khz(CPU_CLOCK_MHZ * 1000, true);
    sleep_ms(10);
#else // RP2040
    // No vreg/QMI games: 125 MHz default-class clock is safe with the
    // SDK's stock boot2 flash timing on the pico board.
    sleep_ms(10);
    set_sys_clock_khz(CPU_CLOCK_MHZ * 1000, true);
    sleep_ms(10);
#endif
}

//=============================================================================
// Image validation (PICOBIN header)
//=============================================================================

typedef enum {
    IMG_OK = 0,
    IMG_NO_HEADER,   // no PICOBIN marker (RP2040 image, old or murky bin)
    IMG_CHUNK_BAD,   // marker found but IMAGE_TYPE item is not EXE|S|ARM|RP2350
    IMG_SIZE_BAD,    // empty or beyond the 1 MiB budget
    IMG_UF2_MAGIC,   // bad UF2 block magic (start or end)
    IMG_UF2_FAMILY,  // RP2040 or unknown UF2 family (RP2350 only accepted)
    IMG_UF2_SEQ,     // payload blocks not addr 0x10000000 step 256 sequential
    IMG_UF2_EMPTY,   // no payload blocks in the file
} img_check_t;

// family of the last refused UF2 block, for the UI message
static uint32_t g_uf2_bad_family = 0;

static const char *img_check_str(img_check_t t) {
    switch (t) {
        case IMG_OK:          return "OK";
        case IMG_NO_HEADER:   return "no PICOBIN header (RP2040 image or bad bin)";
        case IMG_CHUNK_BAD:   return "IMAGE_TYPE item is not EXE|Secure|ARM|RP2350";
        case IMG_SIZE_BAD:    return "empty image or > 1 MiB budget";
        case IMG_UF2_MAGIC:   return "bad UF2 block magic (not a fork-layout .uf2)";
        case IMG_UF2_FAMILY:
#if PICO_RP2350
            if (g_uf2_bad_family == UF2_FAMILY_RP2040)
                return "img RP2040 will not be used";
            return "unknown UF2 family (RP2350 0xE48BFF59 only)";
#else
            if (g_uf2_bad_family == UF2_FAMILY_RP2350)
                return "img RP2350 will not be used";
            return "unknown UF2 family (RP2040 0xE48BFF56 only)";
#endif
        case IMG_UF2_SEQ:     return "UF2 payload not sequential 0x10000000 step 256";
        case IMG_UF2_EMPTY:   return "UF2 file contains no payload blocks";
    }
    return "?";
}

// Reads the first 512 bytes and parses the PICOBIN header. Either a valid
// EXE|Secure|ARM|RP2350 image block (as emitted by pico-sdk 2.x pico2) is
// found, or the image is refused. An RP2040 .bin has no PICOBIN header, so
// it can never pass this check.
static img_check_t check_image(FIL *file) {
    static uint8_t head[512]; // SRAM only, never touched during flash window
    UINT rd = 0;
    if (f_lseek(file, 0) != FR_OK) return IMG_NO_HEADER;
    if (f_read(file, head, sizeof(head), &rd) != FR_OK || rd != sizeof(head))
        return IMG_NO_HEADER;

    // blank image guard (all 0xFF / erased sector)
    bool blank = true;
    for (uint32_t i = 0; i < 64; i++) if (head[i] != 0xFF) { blank = false; break; }
    if (blank) return IMG_NO_HEADER;

    uint32_t marker_off = 0xffffffffu;
    for (uint32_t o = 0; o + 4 <= sizeof(head); o += 4) {
        uint32_t w;
        memcpy(&w, head + o, 4);
        if (w == PICOBIN_BLOCK_MARKER_START) {
            marker_off = o;
            break;
        }
    }
    if (marker_off == 0xffffffffu) return IMG_NO_HEADER;

    // Layout (verified on the builds in this repo):
    //   marker_off+0 : PICOBIN_MARKER_START (4 bytes, 0xFFFFDED3)
    //   marker_off+4 : first item: {tag 0x42 IMAGE_TYPE, count 0x01,
    //                  hword value 0x1021 = EXE|Secure|ARM|RP2350}
    //   marker_off+8 : LAST item tag (0xFF), then the END marker follows.
    if (head[marker_off + 4] != PICOBIN_BLOCK_ITEM_1BS_IMAGE_TYPE)
        return IMG_CHUNK_BAD;
    if (head[marker_off + 5] != 0x01) // 1 word payload
        return IMG_CHUNK_BAD;
    uint16_t v;
    memcpy(&v, head + marker_off + 6, 2);
    if (v != PICOBIN_IMAGE_TYPE_EXE_S_ARM_RP2350)
        return IMG_CHUNK_BAD;
    return IMG_OK;
}

// Streaming UF2 validation. Reads the .uf2 from the SD block-by-block
// (512B), never buffering the whole file. Rules (repo fork layout);
//   - every block must carry the fork start/end magic,
//   - image-table blocks (addr 0x10FFFF00, flags 0xA000, family 0xE48BFF57)
//     are skipped: they carry no flash payload,
//   - payload blocks must be family RP2350 (0xE48BFF59); RP2040 (0xE48BFF56)
//     or any other family is refused,
//   - payload addr must start at 0x10000000 and step exactly 256 bytes,
//   - derived image size = last addr - 0x10000000 + 256, capped at 1 MiB.
// On success *img_size_out is the number of flash bytes the payload spans
// (multiple of 256, may exceed the original .bin size by trailing padding).
static img_check_t uf2_scan(FIL *file, uint32_t *img_size_out) {
    static uint8_t blk[UF2_BLOCK_SIZE]; // SRAM only, never touched during flash window
    if (f_lseek(file, 0) != FR_OK) return IMG_UF2_MAGIC;
    if (f_size(file) < UF2_BLOCK_SIZE) return IMG_UF2_MAGIC;
    const uint32_t blocks = (uint32_t)f_size(file) / UF2_BLOCK_SIZE;

    uint32_t next_addr = UF2_ADDR_FIRST;
    uint32_t payload_blocks = 0;
    for (uint32_t b = 0; b < blocks; b++) {
        UINT rd = 0;
        if (f_read(file, blk, sizeof(blk), &rd) != FR_OK || rd != UF2_BLOCK_SIZE)
            return IMG_UF2_MAGIC;
        uint32_t m0, m1, flags, addr, psize, fam, mend;
        memcpy(&m0, blk + 0, 4);
        memcpy(&m1, blk + 4, 4);
        if (m0 != UF2_MAGIC_START0 || m1 != UF2_MAGIC_START1)
            return IMG_UF2_MAGIC;
        memcpy(&flags, blk + 8, 4);
        memcpy(&addr, blk + 12, 4);
        memcpy(&psize, blk + 16, 4);
        memcpy(&fam, blk + 28, 4);
        memcpy(&mend, blk + 508, 4);
        if (mend != UF2_MAGIC_END)
            return IMG_UF2_MAGIC;
        if (addr == UF2_ADDR_IMAGE_TABLE) continue; // image-table block: skip
        if (psize != UF2_PAYLOAD || !(flags & UF2_FLAG_FAMILY))
            return IMG_UF2_SEQ;
        if (fam != UF2_FAMILY_TARGET) {
            g_uf2_bad_family = fam;
            return IMG_UF2_FAMILY;
        }
        if (addr != next_addr)
            return IMG_UF2_SEQ;
        next_addr = addr + UF2_PAYLOAD;
        payload_blocks++;
    }
    if (payload_blocks == 0) return IMG_UF2_EMPTY;
    const uint32_t img_size = next_addr - UF2_ADDR_FIRST;
    if (img_size > MAX_IMAGE_SIZE) return IMG_SIZE_BAD;
    *img_size_out = img_size;
    return IMG_OK;
}

//=============================================================================
// Flash programming (chunk-by-chunk, XIP safe)
//=============================================================================

// 4 KiB staging chunk pinned in SRAM (.bss) - the only data touched while
// XIP is halted inside the erase/program window.
static uint8_t g_flash_chunk[FLASH_SECTOR_SIZE] __attribute__((aligned(4)));

static bool flash_program_size_ok(uint32_t size) {
    return size > 0 && size <= MAX_IMAGE_SIZE && size <= FLASH_SIZE_BYTES;
}

// Erase + program one 4 KiB chunk, then VERIFY the just-programmed range by
// reading it back through XIP and comparing against the staging buffer
// (v0.3.2). The SDK flash_range_* helpers go through the ROM flash functions,
// which restore XIP (via the bootram boot2) and flush the XIP cache before
// returning (pico-sdk hardware_flash/flash.c:233-236 + 263-265), so a plain
// post-call read at XIP_BASE + offset is coherent.
// Returns false on a verify mismatch: the caller aborts with the LED error
// code and never reboots (the video is already off by then).
static bool __no_inline_not_in_flash_func(bootman_erase_program_chunk)(
    uint32_t offset, const uint8_t *chunk, uint32_t len) {
    const uint32_t state = save_and_disable_interrupts();
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
    flash_range_program(offset, chunk, len); // len padded to 256B by the caller
    restore_interrupts(state);

    // v0.3.2 verify: XIP_BASE + offset maps flash offset directly.
    return memcmp((const uint8_t *)(XIP_BASE + offset), chunk, len) == 0;
}

// Pad the chunk buffer up to the 256-byte flash program granularity with 0xFF
static uint32_t bootman_pad_program_len(uint32_t len) {
    len = (len + 255u) & ~255u;
    if (len > FLASH_SECTOR_SIZE) len = FLASH_SECTOR_SIZE;
    memset(g_flash_chunk + len, 0xFF, FLASH_SECTOR_SIZE - len);
    return len;
}

// Quiesce everything that can touch flash while XIP is halted:
// (HDMI only) reset core1 which runs the video task/IRQ, stop video DMA +
// PIO SM, abort every DMA channel that may still be armed, mask the DMA IRQ
// lines.
static void bootman_quiesce(void) {
#if defined(VIDEO_HDMI)
    // core1 only runs the HDMI video task; kill it before pausing the
    // scanout so no IRQ/DMA handler can touch flash-adjacent state.
    multicore_reset_core1();
#endif
    graphics_shutdown();
    for (uint32_t ch = 0; ch < NUM_DMA_CHANNELS; ++ch) {
        dma_channel_abort((uint)ch);
    }
#if PICO_RP2350
    // Mirror of c64/src/rp2350/bootSwap.c:180-182: RP2350 has DMA_IRQ_0..3.
    // v0.3.1 masked only lines 0/1 - lines 2/3 stayed enabled across the
    // XIP-halted window.
    for (uint32_t i = 0; i < 4; ++i) { // DMA_IRQ_0..3 (RP2350)
#else
    for (uint32_t i = 0; i < 2; ++i) { // DMA_IRQ_0 / DMA_IRQ_1 (RP2040)
#endif
        irq_set_enabled(DMA_IRQ_0 + i, false);
    }
}

//=============================================================================
// Flash-phase diagnostics + safe flash clock (v0.3.2)
//=============================================================================

// During the erase/program window the video driver is shut down
// (bootman_quiesce()), so the on-board LED is the ONLY status channel. The
// M1 carries the standard activity LED on GPIO 25 (PICO_DEFAULT_LED_PIN
// from the pico2 board header, boards/pico2.h:38; the RP2040 pico board
// header defines the same pin). This is separate from the vendored HDMI
// driver's __led_state screen overlay, whose framebuffer dies with the
// video (that variable stays declared for the vendored driver's extern).
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 25 // standard RP2350/RP2040 activity LED
#endif
#define BOOTMAN_LED_PIN PICO_DEFAULT_LED_PIN

// Second, board-visible blink channel: on the Murmulator M1 the ILI9341 TFT
// backlight is on GPIO 9. During the flash phase the video is off (see
// bootman_quiesce()), so GPIO 9 is free, and the board reboots right after
// the flash succeeds - no cleanup needed.
//
// CONFLICT NOTE: GPIO 9 is inside the shared M1 video-connector range
// (HDMI_BASE_PIN=6, 8 consecutive pins 6..13, src/board_config.h:43) which is
// claimed by EVERY bootman video variant while the screen is up:
//   - RP2350 VGA: pio_gpio_init on VGA_BASE_PIN..+7 (vga_bootman.c:272-276)
//   - RP2350 HDMI: clk pair 6..7 + TMDS data pins 8..13 (HDMI.c:516-545)
//   - RP2040 VGA: same M1 layout via the shared src/board_config.h
// (SD 2..5, PS/2 0..1, NES 14..16 do not touch GPIO 9.) Because the pin is
// a video pin in ALL variants (not only the non-VGA ones), the backlight
// blinks are enabled ONLY for the VGA RP2350 build - the configuration this
// feature targets, where every blink happens after bootman_quiesce() has
// silenced the scanout.
#if defined(VIDEO_VGA) && PICO_RP2350
#define BOOTMAN_DIAG_TFT_BL_PIN 9 // Murmulator M1: ILI9341 TFT backlight - visible blink channel during flash phase
#endif

static void bootman_led_init(void) {
    gpio_init(BOOTMAN_LED_PIN);
    gpio_set_dir(BOOTMAN_LED_PIN, GPIO_OUT);
    gpio_put(BOOTMAN_LED_PIN, 0);
#ifdef BOOTMAN_DIAG_TFT_BL_PIN
    gpio_init(BOOTMAN_DIAG_TFT_BL_PIN);
    gpio_set_dir(BOOTMAN_DIAG_TFT_BL_PIN, GPIO_OUT);
    gpio_put(BOOTMAN_DIAG_TFT_BL_PIN, 0); // BL LOW = backlight off
#endif
}

// The video drivers mux GPIO 9 to PIO for the shared video bus while the
// screen is up (pio_gpio_init, vga_bootman.c:275), and graphics_shutdown()
// only stops the SM - it never restores the SIO function, so a plain
// gpio_put() on the pad would be ignored. Re-claim the pin for SIO right
// after bootman_quiesce() so every blink below actually drives the
// backlight. (The activity LED on GPIO 25 is never claimed by any driver.)
static void bootman_tft_bl_arm(void) {
#ifdef BOOTMAN_DIAG_TFT_BL_PIN
    gpio_set_function(BOOTMAN_DIAG_TFT_BL_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(BOOTMAN_DIAG_TFT_BL_PIN, GPIO_OUT);
    gpio_put(BOOTMAN_DIAG_TFT_BL_PIN, 0);
#endif
}

// Busy-wait blip (busy_wait_ms polls the hardware timer - no IRQs needed;
// they are irrelevant/masked in this phase anyway). Both diagnostic pins
// (activity LED + TFT backlight) toggle together.
static void bootman_led_blip(uint32_t on_ms, uint32_t off_ms) {
    gpio_put(BOOTMAN_LED_PIN, 1);
#ifdef BOOTMAN_DIAG_TFT_BL_PIN
    gpio_put(BOOTMAN_DIAG_TFT_BL_PIN, 1); // backlight ON
#endif
    busy_wait_ms(on_ms);
    gpio_put(BOOTMAN_LED_PIN, 0);
#ifdef BOOTMAN_DIAG_TFT_BL_PIN
    gpio_put(BOOTMAN_DIAG_TFT_BL_PIN, 0); // backlight OFF
#endif
    busy_wait_ms(off_ms);
}

// 1 long blip (500 ms): right when the erase/program phase starts.
static void bootman_led_start(void) {
    bootman_tft_bl_arm(); // re-claim GPIO 9 from the video PIO (video now off)
    bootman_led_blip(500, 150);
}

// 1 short blip (60 ms): progress heartbeat, called every 16 flashed chunks.
static void bootman_led_progress(void) {
    bootman_led_blip(60, 150);
}

// 2 short blips (150 ms): full success - caller proceeds to watchdog_reboot.
static void bootman_led_success(void) {
    bootman_led_blip(150, 150);
    bootman_led_blip(150, 150);
}

// Continuous fast blinking (10 Hz) forever: SD read failure mid-flash. With
// the video already off, a hold_screen message would draw into a dead
// framebuffer; the LED is the signal. Never returns (BOOTSEL recovery only).
__attribute__((noreturn))
static void bootman_led_sd_read_failed(void) {
    while (true) {
        gpio_put(BOOTMAN_LED_PIN, 1);
#ifdef BOOTMAN_DIAG_TFT_BL_PIN
        gpio_put(BOOTMAN_DIAG_TFT_BL_PIN, 1); // backlight ON
#endif
        busy_wait_ms(50);
        gpio_put(BOOTMAN_LED_PIN, 0);
#ifdef BOOTMAN_DIAG_TFT_BL_PIN
        gpio_put(BOOTMAN_DIAG_TFT_BL_PIN, 0); // backlight OFF
#endif
        busy_wait_ms(50);
    }
}

// 3 blips (150 ms) repeating forever: verify mismatch / mid-flash abort.
// Slot 0 is corrupt - never reboot into a bogus image; BOOTSEL recovery
// only. Never returns.
__attribute__((noreturn))
static void bootman_led_verify_failed(void) {
    while (true) {
        for (int i = 0; i < 3; i++) {
            bootman_led_blip(150, 150);
        }
        busy_wait_ms(300);
    }
}

// v0.3.2: drop to the SDK-safe 125 MHz clock before ANY flash erase/program,
// called right AFTER bootman_quiesce() (video/DMA parked). The known-good
// reference c64/src/rp2350/bootSwap.c touches NO clock at all: it flashes at
// that firmware's boot clock (CPU_CLOCK_MHZ 252, c64/src/board_config.h:45).
// Bootman boots at 378 MHz (CPU_VOLTAGE 1.60 + QMI M0 timing set for >252
// MHz) - a configuration with NO hardware-verified flash history in this
// repo, so use the SDK's stock operating point: the ROM flash helpers used
// by flash_range_erase/program re-init QMI M0 themselves anyway (pico-sdk
// hardware_flash/flash.c:214-238 via the bootram boot2), and 125 MHz makes
// the PIO-SD timing more relaxed, not tighter.
static void bootman_flash_safe_clock(void) {
    if (clock_get_hz(clk_sys) != 125000000u) {
        set_sys_clock_khz(125000, true); // required=true: panic if impossible
    }
}

typedef enum {
    FLASH_OK = 0,
    FLASH_SIZE,     // rejected before any erase
    FLASH_MAGIC,    // PICOBIN/UF2 verification refused: nothing erased
    FLASH_BIN_NA,   // RP2040 manager: raw .bin refused before any erase
    FLASH_HDR,      // f_open failure: nothing erased
    FLASH_SEEK,     // v0.3.2: .bin rewind after check_image failed: nothing
                    // erased (video still up at that point)
    FLASH_SD_READ,  // mid-programming SD failure: sectors may be overwritten
    FLASH_UF2_SEQ,  // mid-programming UF2 re-check failed: file changed / SD
                    // returned different data - slot may be corrupt
} flash_result_t;

static const char *flash_result_str(flash_result_t r) {
    switch (r) {
        case FLASH_OK:       return "flash ok";
        case FLASH_SIZE:     return "size budget refused (empty or > 1 MiB)";
        case FLASH_MAGIC:    return "image refused (no magic / wrong chip)";
        case FLASH_BIN_NA:   return "RP2040: flash using .uf2 only";
        case FLASH_HDR:      return "could not open file on SD";
        case FLASH_SEEK:     return "could not rewind the image file on SD";
        case FLASH_SD_READ:  return "SD read FAILED mid-flash - slot 0 may be corrupt";
        case FLASH_UF2_SEQ:  return "UF2 stream changed mid-flash - slot 0 may be corrupt";
    }
    return "?";
}

// One per-block UF2-level check of a block the flash stream is about to
// ingest (same rules as the pre-flight uf2_scan).
static bool uf2_block_payload_ok(const uint8_t *blk, uint32_t next_addr) {
    uint32_t m0, m1, flags, addr, psize, fam, mend;
    memcpy(&m0, blk + 0, 4);
    memcpy(&m1, blk + 4, 4);
    memcpy(&flags, blk + 8, 4);
    memcpy(&addr, blk + 12, 4);
    memcpy(&psize, blk + 16, 4);
    memcpy(&fam, blk + 28, 4);
    memcpy(&mend, blk + 508, 4);
    if (m0 != UF2_MAGIC_START0 || m1 != UF2_MAGIC_START1) return false;
    if (mend != UF2_MAGIC_END) return false;
    uint32_t a;
    memcpy(&a, blk + 12, 4);
    if (a == UF2_ADDR_IMAGE_TABLE) return true; // image-table kind, no payload
    if (psize != UF2_PAYLOAD || !(flags & UF2_FLAG_FAMILY)) return false;
    if (fam != UF2_FAMILY_TARGET) return false;
    if (a != next_addr) return false;
    return true;
}

// Program a .uf2 by re-reading it from the SD at flash time and writing it
// into slot 0 sector-by-sector. The file is validated a second time while
// streaming (same rules as the pre-flight uf2_scan). Payload bytes of
// consecutive 256-byte UF2 blocks are accumulated into the single 4 KiB
// staging chunk; when the chunk is full (or the file ends) it is erased +
// programmed - no full-file buffer in RAM.
static flash_result_t flash_uf2_stream(FIL *file) {
    static uint8_t blk[UF2_BLOCK_SIZE]; // SRAM only, never touched while XIP halted

    // v0.3.1 FIX: uf2_scan() consumed the file up to EOF, so the stream must
    // rewind to block 0 before the flash-time re-read (mirrors uf2_scan's
    // f_lseek, line ~513). Missing this rewind made the first f_read return
    // 0 bytes -> FLASH_SD_READ -> hold_screen with video already off ->
    // permanent black; the flash never executed.
    if (f_lseek(file, 0) != FR_OK)
        return FLASH_SD_READ;

    const uint32_t fsize = (uint32_t)f_size(file);
    const uint32_t blocks = fsize / UF2_BLOCK_SIZE;

    uint32_t next_addr = UF2_ADDR_FIRST; // flash addr of the next payload
    uint32_t filled = 0;                 // bytes pending in g_flash_chunk
    bool saw_payload = false;

    // v0.3.1 SAFETY: pull the FIRST block while video is still up. Any read
    // failure here is pre-flight (nothing erased yet), so its message stays
    // visible instead of dying into a black screen. The first erase/program
    // can only happen from block 17 on, so quiescing after one read is safe.
    UINT rd = 0;
    if (blocks > 0 &&
        (f_read(file, blk, sizeof(blk), &rd) != FR_OK || rd != UF2_BLOCK_SIZE))
        return FLASH_SD_READ; // nothing erased, video still on

    // Confirmation was taken by the caller; quiesce video/DMA and go.
    bootman_quiesce();

    // v0.3.2: video is OFF from here on - the LED is the only status
    // channel. Drop to the SDK-safe 125 MHz flash clock, then one long blip
    // marks the start of the erase/program phase (the first chunk is flashed
    // once the stream fills a full 4 KiB below).
    bootman_flash_safe_clock();
    bootman_led_start();

    uint32_t flushed = 0; // number of 4 KiB chunks already flashed (heartbeat)

    for (uint32_t b = 0; b < blocks; b++) {
        if (b != 0) { // first block was already read above (video still on)
            rd = 0;
            if (f_read(file, blk, sizeof(blk), &rd) != FR_OK || rd != UF2_BLOCK_SIZE) {
                // Mid-loop SD failure with the video already off: some
                // chunks may already be overwritten. Do NOT reboot; signal
                // 10 Hz LED forever for BOOTSEL recovery.
                f_close(file);
                bootman_led_sd_read_failed();
            }
        }
        if (!uf2_block_payload_ok(blk, next_addr)) {
            // Mid-stream re-check failed (file changed / SD returned
            // different data) with the video already off. Slot 0 may be
            // corrupt - hold with the verify-class LED abort, never reboot.
            f_close(file);
            bootman_led_verify_failed();
        }
        // image-table block (addr 0x10FFFF00): valid, no flash data
        uint32_t addr;
        memcpy(&addr, blk + 12, 4);
        if (addr == UF2_ADDR_IMAGE_TABLE) continue;

        memcpy(g_flash_chunk + filled, blk + 32, UF2_PAYLOAD);
        filled += UF2_PAYLOAD;
        next_addr = addr + UF2_PAYLOAD;
        saw_payload = true;

        if (filled == FLASH_SECTOR_SIZE) {
            if (!bootman_erase_program_chunk(next_addr - filled - UF2_ADDR_FIRST,
                                             g_flash_chunk, FLASH_SECTOR_SIZE)) {
                // Post-program verify mismatch: slot 0 is corrupt - LED
                // abort, never reboot into a bogus image.
                f_close(file);
                bootman_led_verify_failed();
            }
            filled = 0;
            if ((++flushed & 15u) == 0) {
                bootman_led_progress(); // 60ms heartbeat every 16 chunks
            }
        }
    }
    if (!saw_payload) {
        // Unreachable-checked: uf2_scan() pre-flight refuses empty files.
        // Belt-and-braces with the video off: LED abort instead of a silent
        // black hold.
        f_close(file);
        bootman_led_verify_failed();
    }
    if (filled != 0) {
        const uint32_t len = bootman_pad_program_len(filled);
        if (!bootman_erase_program_chunk(next_addr - filled - UF2_ADDR_FIRST,
                                         g_flash_chunk, len)) {
            f_close(file); // final chunk verify mismatch: LED abort
            bootman_led_verify_failed();
        }
    }

    // v0.3.2: full success - 2 short blips, then the caller (main) reboots.
    bootman_led_success();
    return FLASH_OK;
}

// High-level flash entry: validates (.bin PICOBIN or .uf2 fork-layout scan),
// then wipes + stages the image in slot 0.
static flash_result_t flash_image_from_sd(int idx) {
    FIL file;
    char path[MAX_NAME + sizeof(IMAGE_DIR)];
    snprintf(path, sizeof(path), "%s/%s", IMAGE_DIR, g_files[idx].name);

    FRESULT fr = f_open(&file, path, FA_READ);
    if (fr != FR_OK) {
        MII_DEBUG_PRINTF("bootman: f_open failed %d\n", (int)fr);
        return FLASH_HDR;
    }

#if !PICO_RP2350
    // RP2040 manager: raw .bin images are refused BEFORE any erase - RP2040
    // firmware bins start with boot2 (no PICOBIN header), so there is no
    // safe magic to verify against; only family-checked .uf2 is allowed.
    if (!g_files[idx].is_uf2) {
        f_close(&file);
        return FLASH_BIN_NA;
    }
#endif

    if (g_files[idx].is_uf2) {
        // Pre-flight: streaming UF2 verification - nothing erased on refusal.
        // Budget check uses the DERIVED image size (payload span), never the
        // raw file size: a 1 MiB .uf2 grows to ~2.4 MiB on the FAT.
        uint32_t img_size = 0;
        img_check_t im = uf2_scan(&file, &img_size);
        if (im != IMG_OK) {
            f_close(&file);
            return FLASH_MAGIC;
        }
        if (!flash_program_size_ok(img_size)) {
            f_close(&file);
            return FLASH_SIZE;
        }
        // Re-read the .uf2 from the SD and program it chunk-by-chunk.
        const flash_result_t fr2 = flash_uf2_stream(&file);
        f_close(&file);
        return fr2;
    }

    const uint32_t size = (uint32_t)f_size(&file);
    if (!flash_program_size_ok(size)) {
        f_close(&file);
        return FLASH_SIZE;
    }
    // Pre-flight: PICOBIN magic verification - nothing erased on refusal.
    img_check_t im = check_image(&file);
    if (im != IMG_OK) {
        f_close(&file);
        return FLASH_MAGIC;
    }

    // v0.3.2 MIRROR of c64/src/rp2350/bootSwap.c:78: check_image() consumed
    // the first 512 bytes (f_lseek 0 + f_read), so rewind to offset 0 before
    // streaming the image. Without this rewind every chunk below was read
    // 512-byte-shifted and the final f_read came up 512 bytes short at EOF
    // -> FLASH_SD_READ while the video was already off -> permanent black
    // with a partly/wholly unprogrammed slot 0. Nothing is erased here, so
    // with the video still up the failure can use the visible hold_screen.
    if (f_lseek(&file, 0) != FR_OK) {
        f_close(&file);
        return FLASH_SEEK;
    }

    // Confirmation was taken by the caller; quiesce video/DMA and go.
    bootman_quiesce();

    // v0.3.2: video is OFF from here on - the LED is the only status
    // channel. Drop to the SDK-safe 125 MHz flash clock (as analyzed against
    // bootSwap.c: the reference does not change the clock; see the comment
    // on bootman_flash_safe_clock), then one long blip marks the start of
    // the erase/program phase.
    bootman_flash_safe_clock();
    bootman_led_start();

    uint32_t done = 0;
    while (done < size) {
        uint32_t n = size - done;
        if (n > FLASH_SECTOR_SIZE) n = FLASH_SECTOR_SIZE;
        UINT rd = 0;
        if (f_read(&file, g_flash_chunk, n, &rd) != FR_OK || rd != n) {
            // Mid-flash SD failure: some sectors may already be overwritten;
            // do NOT reboot - with the video off, hold_screen would draw into
            // a dead framebuffer: signal 10 Hz LED forever (BOOTSEL recovery).
            f_close(&file);
            bootman_led_sd_read_failed();
        }
        const uint32_t len = bootman_pad_program_len(n);
        if (!bootman_erase_program_chunk(done, g_flash_chunk, len)) {
            // Post-program verify mismatch: slot 0 is corrupt; do NOT reboot
            // (a bogus image must never boot) - 3x150 ms LED repeating.
            f_close(&file);
            bootman_led_verify_failed();
        }
        done += n;
        if ((done & (FLASH_SECTOR_SIZE * 16u - 1u)) == 0) {
            bootman_led_progress(); // 60ms heartbeat every 16 chunks (64 KiB)
        }
    }
    f_close(&file);

    // v0.3.2: full success - 2 short blips, then main() reboots via
    // watchdog_reboot(0, 0, 100) below.
    bootman_led_success();
    return FLASH_OK;
}

//=============================================================================
// Chip detection (compile-time platform + package selector)
//=============================================================================

static void get_chip_label(char *buf, unsigned len) {
#if PICO_RP2350
    uint32_t package_sel =
        *((io_ro_32 *)(SYSINFO_BASE + SYSINFO_PACKAGE_SEL_OFFSET));
    bool pkg_a = (package_sel & 1) != 0;
    snprintf(buf, len, "RP2350%s (pkg_sel=%lu)", pkg_a ? "-A" : "-B",
             (unsigned long)package_sel);
#else
    snprintf(buf, len, "RP2040");
#endif
}

//=============================================================================
// Hold-screen helpers
//=============================================================================

static void hold_screen(const char *msg1, const char *msg2, const char *msg3,
                        flash_result_t res) {
    fb_text(9, "REFUSED:", C_RED);
    fb_text(10, msg1, C_RED);
    fb_text(11, msg2 ? msg2 : "", C_RED);
    if (msg3) fb_text(12, msg3, C_RED);
    fb_text(BOOTMAN_ROWS - 1,
            (res == FLASH_SD_READ || res == FLASH_UF2_SEQ)
              ? "HOLDING - hold BOOTSEL while replugging USB"
              : "hold BOOTSEL to flash another image",
            C_RED);
    while (true) {
        tight_loop_contents();
    }
}

//=============================================================================
// main
//=============================================================================

#if defined(VIDEO_HDMI)
// HDMI video task on core1 (same scheme as FRANK C64: the DMA IRQ handler
// and DMA start live on core1 so video keeps running while core0 does the
// browser/SD work). Reset from core0 in bootman_quiesce() before flash.
static void bootman_video_core1(void) {
    graphics_init_irq_on_this_core();
    while (true) {
        tight_loop_contents();
    }
}
#endif

int main(void) {
    init_clocks();

    // v0.3.2: flash-phase diagnostics pins - activity LED (GPIO 25) on every
    // variant, plus the Murmulator M1 ILI9341 TFT backlight (GPIO 9) on the
    // VGA RP2350 build; both defined and initialised up front so the helpers
    // below never touch an unclaimed pin during the video-off window.
    bootman_led_init();

#if ENABLE_DEBUG_LOGS
    stdio_init_all();
#endif

    // video
    fb_clear();
#if defined(VIDEO_HDMI)
    // claim PIO1/DMA on core0, defer IRQ + DMA start to the core1 task
    graphics_set_defer_irq_to_core1(true);
    graphics_init(g_out_HDMI);
#else
    graphics_init();
#endif
    graphics_set_res(BOOTMAN_FB_WIDTH, BOOTMAN_FB_HEIGHT);
    graphics_set_palette(C_BLACK,  0x000000u);
    graphics_set_palette(C_WHITE,  0xffffffu);
    graphics_set_palette(C_GREY,   0xaaaaaau);
    graphics_set_palette(C_GREEN,  0x00c000u);
    graphics_set_palette(C_YELLOW, 0xf0f000u);
    graphics_set_palette(C_RED,    0xf00000u);
    graphics_set_bgcolor(0x000000u);
#if defined(VIDEO_HDMI)
    // palette is in place - now let core1 start the scanout
    multicore_launch_core1(bootman_video_core1);
    for (uint32_t i = 0; i < 500 && !get_frame_count(); i++) {
        sleep_ms(10); // wait for first frame (max ~5 s)
    }
#endif

    char chip_label[48];
    get_chip_label(chip_label, sizeof(chip_label));

    // input
    nespad_begin(CPU_CLOCK_MHZ * 1000, NESPAD_GPIO_CLK, NESPAD_GPIO_DATA, NESPAD_GPIO_LATCH);
#if ENABLE_PS2_KEYBOARD
    ps2kbd_init();
#endif

    // SD + FAT
    bool sd_ok = (f_mount(&g_fatfs, "", 1) == FR_OK);
    if (sd_ok) scan_image_dir();

    draw_browser(chip_label, sd_ok);

    bool in_confirm = false;
    uint32_t nes_prev = 0;
    bool browser_running = true;
    nespad_state = 0;

    while (browser_running) {
        nespad_read();
        uint32_t nes_now = nespad_state;
        uint32_t nes_press = nes_now & ~nes_prev;
        nes_prev = nes_now;

#if ENABLE_PS2_KEYBOARD
        ps2kbd_tick();
        uint8_t k = 0;
        bool have_key = ps2_key_take(&k);
#else
        uint8_t k = 0;
        bool have_key = false;
#endif

        if (in_confirm) {
            bool yes = (have_key && k == 0x0D) || (nes_press & (DPAD_A | DPAD_START));
            bool no  = (have_key && k == 0x1B) || (nes_press & DPAD_B);
            if (no) {
                in_confirm = false;
                draw_browser(chip_label, sd_ok);
            } else if (yes) {
                browser_running = false;
            }
        } else {
            bool up   = (have_key && k == 0x0B) || (nes_press & DPAD_UP);
            bool down = (have_key && k == 0x0A) || (nes_press & DPAD_DOWN);
            bool ok   = (have_key && k == 0x0D) || (nes_press & (DPAD_A | DPAD_START));
            if (up && g_file_count > 0 && g_selected > 0) {
                g_selected--;
                if (g_selected < g_scroll_top) g_scroll_top = g_selected;
                draw_browser(chip_label, sd_ok);
            }
            if (down && g_file_count > 0 && g_selected + 1 < g_file_count) {
                g_selected++;
                if (g_selected >= g_scroll_top + LIST_ROWS)
                    g_scroll_top = g_selected - LIST_ROWS + 1;
                draw_browser(chip_label, sd_ok);
            }
            if (ok && g_file_count > 0) {
                draw_confirm(g_selected);
                in_confirm = true;
            }
        }
        sleep_ms(20);
    }

    // ---- flash flow (after the explicit confirm) ----
    fb_text(9, "pre-flight check...", C_GREY);

    flash_result_t res = flash_image_from_sd(g_selected);

    if (res != FLASH_OK && res != FLASH_SD_READ && res != FLASH_UF2_SEQ) {
        // pre-erase rejection: bootman stays alive and valid
        hold_screen(flash_result_str(res), NULL, NULL, res);
    }
    if (res == FLASH_SD_READ || res == FLASH_UF2_SEQ) {
        hold_screen(flash_result_str(res),
                    "RECOVERY: hold BOOTSEL while replugging USB",
                    "do NOT power-cycle as a fix; BOOTSEL always works", res);
    }

    fb_text(9, "flash ok - rebooting into the new image...", C_GREEN);
    watchdog_reboot(0, 0, 100);
    while (true) {
        tight_loop_contents();
    }
    __builtin_unreachable();
}
