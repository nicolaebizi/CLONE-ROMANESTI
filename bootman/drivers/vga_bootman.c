/*
 *  vga_bootman.c - minimal 320x240 8bpp VGA scanout for BOOTMAN
 *
 *  Adapted (trimmed) from FRANK C64 drivers/vga.c - same PIO out-stream +
 *  DMA chain scheme, 640x480@60, lines doubled in the ISR, palette lookup
 *  per pixel from a 320-byte 8bpp line buffer provided by graphics_get_buffer_line().
 *
 *  Differences from the FRANK C64 original:
 *   - no HDMI driver dependency (VGA only);
 *   - no g_out parameter, no IRQ deferral (single core use);
 *   - graphics_shutdown() added so the boot manager can quiesce the video
 *     DMA before the flash erase/program window (XIP halt safety).
 *
 *  BOOTMAN - standalone boot manager for Murmulator RP2350
 */
#include "hardware/clocks.h"
#include "stdbool.h"
#include "hardware/structs/pll.h"
#include "hardware/structs/systick.h"

#include "hardware/dma.h"
#include "hardware/irq.h"
#include <string.h>
#include <stdio.h>
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "stdlib.h"
#include "vga_bootman.h"

uint16_t pio_program_VGA_instructions[] = {
    //     .wrap_target
    0x6008, //  0: out    pins, 8
    //     .wrap
};

const struct pio_program pio_program_VGA = {
    .instructions = pio_program_VGA_instructions,
    .length = 1,
    .origin = -1,
};

static uint32_t* lines_pattern[4];
static uint32_t* lines_pattern_data = NULL;
static int _SM_VGA = -1;

static int N_lines_total = 525;
static int N_lines_visible = 480;
static int line_VS_begin = 490;
static int line_VS_end = 491;
static int shift_picture = 0;

static int visible_line_size = 320;

static dma_channel_config c0, c1;
static int dma_chan_ctrl;
static int dma_chan;
static bool channels_claimed = false;

static uint32_t graphics_buffer_width = 0;
static uint32_t graphics_buffer_height = 0;

// 256-entry 6:6:6-style palette (interleaved line alternation kept from the
// original driver).
static uint16_t palette[2][256];

static uint32_t bg_color[2];
static uint16_t palette16_mask = 0;

static volatile uint32_t graphics_frame_count = 0;

uint32_t get_frame_count(void) {
    return graphics_frame_count;
}

static void vsync_handler(void) {
    // Called from DMA IRQ at frame boundary.
    graphics_frame_count++;
}

extern uint8_t* graphics_get_buffer_line(int y);

void __time_critical_func() dma_handler_VGA() {
    dma_hw->ints0 = 1u << dma_chan_ctrl;
    static uint32_t screen_line = 0;
    screen_line++;

    if (screen_line == N_lines_total) {
        screen_line = 0;
        vsync_handler();
    }

    if (screen_line >= N_lines_visible) {
        // fill with background color
        if (screen_line == N_lines_visible | screen_line == N_lines_visible + 3) {
            uint32_t* output_buffer_32bit = lines_pattern[2 + (screen_line & 1)];
            output_buffer_32bit += shift_picture / 4;
            uint32_t color32 = bg_color[0];
            for (int i = visible_line_size / 2; i--;) {
                *output_buffer_32bit++ = color32;
            }
        }

        // sync signals
        if (screen_line >= line_VS_begin && screen_line <= line_VS_end)
            dma_channel_set_read_addr(dma_chan_ctrl, &lines_pattern[1], false); // VS SYNC
        else
            dma_channel_set_read_addr(dma_chan_ctrl, &lines_pattern[0], false);
        return;
    }

    uint32_t* * output_buffer = &lines_pattern[2 + (screen_line & 1)];
    int y = screen_line >> 1;

    if (y < 0) {
        dma_channel_set_read_addr(dma_chan_ctrl, &lines_pattern[0], false);
        return;
    }
    if ((int)y >= (int)graphics_buffer_height) {
        // fill line with background color
        if (y == graphics_buffer_height | y == graphics_buffer_height + 1 |
            y == graphics_buffer_height + 2) {
            uint32_t* output_buffer_32bit = *output_buffer;
            uint32_t color32 = bg_color[0];
            output_buffer_32bit += shift_picture / 4;
            for (int i = visible_line_size / 2; i--;) {
                *output_buffer_32bit++ = color32;
            }
        }
        dma_channel_set_read_addr(dma_chan_ctrl, output_buffer, false);
        return;
    }

    // image area
    register uint16_t* output_buffer_16bit = (uint16_t *)(*output_buffer);
    output_buffer_16bit += shift_picture / 2;

    uint max_width = graphics_buffer_width;

    uint16_t* current_palette = palette[screen_line & 1];

    // 8-bit buf
    register uint8_t* input_buffer = graphics_get_buffer_line(y);
    for (register int x = max_width; x--;) {
        *output_buffer_16bit++ = current_palette[*input_buffer];
        input_buffer++;
    }
    dma_channel_set_read_addr(dma_chan_ctrl, output_buffer, false);
}

void graphics_set_mode() {
    if (_SM_VGA < 0) return;

    // Already initialized - done
    if (lines_pattern_data) {
        return;
    };
    uint8_t TMPL_VHS8 = 0;
    uint8_t TMPL_VS8 = 0;
    uint8_t TMPL_HS8 = 0;
    uint8_t TMPL_LINE8 = 0;

    int line_size;
    double fdiv = 100;
    int HS_SIZE = 4;
    int HS_SHIFT = 100;

    TMPL_LINE8 = 0b11000000;
    HS_SHIFT = 328 * 2;
    HS_SIZE = 48 * 2;

    line_size = 400 * 2;

    shift_picture = line_size - HS_SHIFT;

    palette16_mask = 0xc0c0;

    visible_line_size = 320;

    N_lines_total = 525;
    N_lines_visible = 480;
    line_VS_begin = 490;
    line_VS_end = 491;

    fdiv = clock_get_hz(clk_sys) / 25175000.0; // pixel clock

    // palette sync-mask correction
    bg_color[0] = bg_color[0] & 0x3f3f3f3f | palette16_mask | palette16_mask << 16;
    bg_color[1] = bg_color[1] & 0x3f3f3f3f | palette16_mask | palette16_mask << 16;
    for (int i = 0; i < 256; i++) {
        palette[0][i] = palette[0][i] & 0x3f3f | palette16_mask;
        palette[1][i] = palette[1][i] & 0x3f3f | palette16_mask;
    }

    // line/sync templates
    {
        const uint32_t div32 = (uint32_t)(fdiv * (1 << 16) + 0.0);
        PIO_VGA->sm[_SM_VGA].clkdiv = div32 & 0xfffff000; // sm clock divider
        dma_channel_set_trans_count(dma_chan, line_size / 4, false);

        lines_pattern_data = (uint32_t *)calloc(line_size * 4 / 4, sizeof(uint32_t));

        for (int i = 0; i < 4; i++) {
            lines_pattern[i] = &lines_pattern_data[i * (line_size / 4)];
        }
        TMPL_VHS8 = TMPL_LINE8 ^ 0b11000000;
        TMPL_VS8 = TMPL_LINE8 ^ 0b10000000;
        TMPL_HS8 = TMPL_LINE8 ^ 0b01000000;

        uint8_t* base_ptr = (uint8_t *)lines_pattern[0];
        // empty line
        memset(base_ptr, TMPL_LINE8, line_size);
        // aligned sync at line start
        memset(base_ptr, TMPL_HS8, HS_SIZE);

        // frame sync
        base_ptr = (uint8_t *)lines_pattern[1];
        memset(base_ptr, TMPL_VS8, line_size);
        memset(base_ptr, TMPL_VHS8, HS_SIZE);

        // blanks for image lines
        base_ptr = (uint8_t *)lines_pattern[2];
        memcpy(base_ptr, lines_pattern[0], line_size);
        base_ptr = (uint8_t *)lines_pattern[3];
        memcpy(base_ptr, lines_pattern[0], line_size);
    }
}

void graphics_set_res(int width, int height) {
    graphics_buffer_width = width;
    graphics_buffer_height = height;
}

void graphics_set_bgcolor(const uint32_t color888) {
    const uint8_t conv0[] = { 0b00, 0b00, 0b01, 0b10, 0b10, 0b10, 0b11, 0b11 };
    const uint8_t conv1[] = { 0b00, 0b01, 0b01, 0b01, 0b10, 0b11, 0b11, 0b11 };

    const uint8_t b = (color888 & 0xff) / 42;

    const uint8_t r = (color888 >> 16 & 0xff) / 42;
    const uint8_t g = (color888 >> 8 & 0xff) / 42;

    const uint8_t c_hi = conv0[r] << 4 | conv0[g] << 2 | conv0[b];
    const uint8_t c_lo = conv1[r] << 4 | conv1[g] << 2 | conv1[b];
    bg_color[0] = ((c_hi << 8 | c_lo) & 0x3f3f | palette16_mask) << 16 |
                  ((c_lo << 8 | c_hi) & 0x3f3f | palette16_mask);
    bg_color[1] = ((c_lo << 8 | c_hi) & 0x3f3f | palette16_mask) << 16 |
                  ((c_hi << 8 | c_lo) & 0x3f3f | palette16_mask);
}

void graphics_set_palette(const uint8_t i, const uint32_t color888) {
    const uint8_t conv0[] = { 0b00, 0b00, 0b01, 0b10, 0b10, 0b10, 0b11, 0b11 };
    const uint8_t conv1[] = { 0b00, 0b01, 0b01, 0b01, 0b10, 0b11, 0b11, 0b11 };

    const uint8_t b = (color888 & 0xff) / 42;

    const uint8_t r = (color888 >> 16 & 0xff) / 42;
    const uint8_t g = (color888 >> 8 & 0xff) / 42;

    const uint8_t c_hi = conv0[r] << 4 | conv0[g] << 2 | conv0[b];
    const uint8_t c_lo = conv1[r] << 4 | conv1[g] << 2 | conv1[b];

    palette[0][i] = (c_hi << 8 | c_lo) & 0x3f3f | palette16_mask;
    palette[1][i] = (c_lo << 8 | c_hi) & 0x3f3f | palette16_mask;
}

void graphics_init_vga() {
    // PIO setup
    const uint offset = pio_add_program(PIO_VGA, &pio_program_VGA);
    _SM_VGA = pio_claim_unused_sm(PIO_VGA, true);
    const uint sm = _SM_VGA;

    for (int i = 0; i < 8; i++) {
        gpio_init(VGA_BASE_PIN + i);
        gpio_set_dir(VGA_BASE_PIN + i, GPIO_OUT);
        pio_gpio_init(PIO_VGA, VGA_BASE_PIN + i);
    }

    pio_sm_set_consecutive_pindirs(PIO_VGA, sm, VGA_BASE_PIN, 8, true);

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + 0, offset + (pio_program_VGA.length - 1));

    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_out_shift(&c, true, true, 32);
    sm_config_set_out_pins(&c, VGA_BASE_PIN, 8);
    pio_sm_init(PIO_VGA, sm, offset, &c);

    pio_sm_set_enabled(PIO_VGA, sm, true);

    // DMA
    dma_chan_ctrl = dma_claim_unused_channel(true);
    dma_chan = dma_claim_unused_channel(true);
    channels_claimed = true;
    // main data channel
    c0 = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&c0, DMA_SIZE_32);

    channel_config_set_read_increment(&c0, true);
    channel_config_set_write_increment(&c0, false);

    uint dreq = DREQ_PIO1_TX0 + sm;
    if (PIO_VGA == pio0) dreq = DREQ_PIO0_TX0 + sm;

    channel_config_set_dreq(&c0, dreq);
    channel_config_set_chain_to(&c0, dma_chan_ctrl);

    dma_channel_configure(
        dma_chan,
        &c0,
        &PIO_VGA->txf[sm], // Write address
        lines_pattern[0], // read address
        600 / 4, //
        false // Don't start yet
    );
    // control channel for the main channel
    c1 = dma_channel_get_default_config(dma_chan_ctrl);
    channel_config_set_transfer_data_size(&c1, DMA_SIZE_32);

    channel_config_set_read_increment(&c1, false);
    channel_config_set_write_increment(&c1, false);
    channel_config_set_chain_to(&c1, dma_chan);

    dma_channel_configure(
        dma_chan_ctrl,
        &c1,
        &dma_hw->ch[dma_chan].read_addr, // Write address
        &lines_pattern[0], // read address
        1, //
        false // Don't start yet
    );

    graphics_set_mode();

    irq_set_exclusive_handler(VGA_DMA_IRQ, dma_handler_VGA);

    dma_channel_set_irq0_enabled(dma_chan_ctrl, true);

    irq_set_enabled(VGA_DMA_IRQ, true);
    dma_start_channel_mask(1u << dma_chan);
}

// Stop the scanout before the flash erase/program window (XIP halt safety):
// disable the DMA IRQ, abort both channels and silence the PIO SM.
void graphics_shutdown(void) {
    if (!channels_claimed) return;
    irq_set_enabled(VGA_DMA_IRQ, false);
    dma_channel_abort(dma_chan);
    dma_channel_abort(dma_chan_ctrl);
    if (_SM_VGA >= 0) {
        pio_sm_set_enabled(PIO_VGA, _SM_VGA, false);
    }
    channels_claimed = false;
    // lines_pattern[0] now points at untouched data; do not restart anything.
}

void adjust_clk(void) {
    double fdiv = clock_get_hz(clk_sys) / 25175000.0;
    const uint32_t div32 = (uint32_t)(fdiv * (1 << 16) + 0.0);
    PIO_VGA->sm[_SM_VGA].clkdiv = div32 & 0xfffff000;
}

void graphics_init(void) {
    graphics_init_vga();
}
