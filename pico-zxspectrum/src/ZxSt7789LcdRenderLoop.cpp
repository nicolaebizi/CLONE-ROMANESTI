// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#include "ZxSt7789LcdRenderLoop.h"
#include "picomputer/picomputer_st7789/st7789_lcd.h"
#include "picomputer/picomputer_st7789/pzx_prepare_rgb444_scanline.h"
#include "pico/stdlib.h"
#include "PicoCharRendererSt7789.h"
#include "PicoCharRendererVga.h"
#include "hardware/clocks.h"

static PIO pio = pio0;
static uint sm = 0;

// --- DIAG: LED heartbeat instrumentation (core1) ---
// Same GPIO 25 as core0's blink_led() in main.cpp (pin already initialised
// there before multicore_launch_core1). Busy-wait only, no tick dependency.
#define DIAG_LED_PIN 25
static void __not_in_flash_func(diag_blink)(int n, uint32_t on_ms, uint32_t off_ms) {
  for (int i = 0; i < n; ++i) {
    gpio_put(DIAG_LED_PIN, 1);
    busy_wait_ms(on_ms);
    gpio_put(DIAG_LED_PIN, 0);
    busy_wait_ms(off_ms);
  }
}

zx_spectrum_audio_driver_enum_t ZxSt7789LcdRenderAudioDefault() {
#if defined(PICO_DEFAULT_AUDIO)
  return PICO_DEFAULT_AUDIO;
#elif defined(PICO_DEFAULT_AUDIO_LCD)
  return PICO_DEFAULT_AUDIO_LCD;
#elif defined(PICO_AUDIO_I2S)
  return zx_spectrum_audio_driver_i2s_index;
#elif defined(PICO_PIO_PWM_AUDIO)
  return zx_spectrum_audio_driver_pio_pwm_index;
#elif defined(PICO_PWM_AUDIO)
  return zx_spectrum_audio_driver_pwm_index;
#else
  return zx_spectrum_audio_driver_pwm_index;
#endif  
}

void ZxSt7789LcdRenderLoopInit() {
  set_sys_clock_khz(200000, true);
  sleep_ms(10);
}

static void ZxSt7789LcdRenderLoopPre(ZxSpectrumMenu& picoRootWin) {
  
  picoRootWin.move(
    LCD_SZ_FRAME_X,
    LCD_SZ_FRAME_Y,
    LCD_SZ_FRAME_COLS,
    LCD_SZ_FRAME_ROWS
  );

  // TODO this is now confusing there are defines in the make files
  // like SZ_WIZ_CW1 that can't be used here as there need to be a 
  // a set for LCD and VGA screens!
  picoRootWin.setWizLayout(0, 13, 18, 40);

  // TODO Configurable backlight control
#ifdef PICOMPUTER_PICOZX_LCD
  // Turn on the LCD backlight
  gpio_init(4);
  gpio_set_dir(4, GPIO_OUT);
  gpio_put(4, 1);
#endif

  // Start up the LCD
  st7789_init(pio, sm);

  // DIAG: st7789_init(pio, sm) returned -> 1 rapid blip
  diag_blink(1, 100, 100);
  sleep_ms(10);
}

void __not_in_flash_func(ZxSt7789LcdRenderLoop)(
    ZxSpectrum &zxSpectrum,
    volatile uint &frames,
    bool &showMenu,
    volatile bool &toggleMenu,
    ZxSpectrumMenu& picoRootWin) 
{
  ZxSt7789LcdRenderLoopPre(picoRootWin);

  uint32_t t1 = time_us_32();
  uint8_t* screenPtr;
  uint8_t* attrPtr;
  uint32_t diagFrames = 0; // DIAG: frame counter for loop instrumentation
  screenPtr = zxSpectrum.screenPtr();
  attrPtr = screenPtr + (32 * 24 * 8);

  while (1) {

    for (uint y = 0; y < 240; ++y) {

      const uint32_t fpf = zxSpectrum.flipsPerFrame();
      const bool ringoMode = fpf > 46 && fpf < 52;
      if (ringoMode) {
        screenPtr = zxSpectrum.memPtr(y & 4 ? 7 : 5);
        attrPtr = screenPtr + (32 * 24 * 8);
      }

      if (y == 0) {

        if (!ringoMode) {
          screenPtr = zxSpectrum.screenPtr();
          attrPtr = screenPtr + (32 * 24 * 8);
        }

        frames++;
      }

      if (showMenu) {
        pcw_send_st7789_scanline(
          pio,
          sm,
          y,
          frames);
      }
      else {
        if (zxSpectrum.cobra80Char()) {
          pzx_send_rgb444_scanline_cobra80(
            pio,
            sm,
            y,
            frames,
            zxSpectrum.cobraVram(),
            zxSpectrum.borderColour(y));
        }
        else {
          pzx_send_rgb444_scanline(
            pio,
            sm,
            y,
            frames,
            screenPtr,
            attrPtr,
            zxSpectrum.borderColour(y));
        }
      }

      ZxRenderLoopCallbackLine(y);
    }

    if (toggleMenu) {
      showMenu = !showMenu;
      toggleMenu = false;
      ZxRenderLoopCallbackMenu(showMenu);
    }

    while((time_us_32() - t1) < 20000) {
      sleep_us(100);
      ZxRenderLoopCallbackLine(-1);
    }

    t1 += 20000;

    // --- DIAG: render loop liveness probes ---
    ++diagFrames;
    if (diagFrames == 1) {
      // DIAG: first complete frame (240 lines + 20ms wait) -> 2 rapid blips
      diag_blink(2, 100, 100);
    }
    else if (diagFrames == 30) {
      // DIAG: 30 frames survived -> 3 rapid blips
      diag_blink(3, 100, 100);
    }
    else if (diagFrames > 30 && ((diagFrames - 30) % 60) == 0) {
      // DIAG: heartbeat, 1 rapid blip every 60 frames (~1s) -> loop stable
      diag_blink(1, 100, 100);
    }
  }
  __builtin_unreachable();
}
