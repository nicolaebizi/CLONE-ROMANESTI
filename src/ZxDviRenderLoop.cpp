// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#include "ZxDviRenderLoop.h"
#include "ZxSpectrumPrepareDviScanline.h"
#include "ZxCzDiag.h"
#include "hardware/clocks.h"
#include "PicoCharRenderer.h"

extern "C" {
    #include "dvi.h"
    #include "dvi_serialiser.h"
    #ifdef DVI_CUSTOM_CONFIG
    #include "hdmi/custom_dvi_pin_configs.h"
    #else
    #include "common_dvi_pin_configs.h"
    #endif
    #include "tmds_encode_zxspectrum.h"
}

struct dvi_inst dvi0;
bool dvi_running = false;

zx_spectrum_audio_driver_enum_t ZxDviRenderLoopAudioDefault() {
#if defined(PICO_DEFAULT_AUDIO)
  return PICO_DEFAULT_AUDIO;
#elif defined(PICO_DEFAULT_AUDIO_DVI)
  return PICO_DEFAULT_AUDIO_DVI;
#elif defined(PICO_HDMI_AUDIO)
  return zx_spectrum_audio_driver_hdmi_index;
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

void ZxDviRenderLoopInit_s(const struct dvi_timing *t) {
  // Run system at TMDS bit clock
  set_sys_clock_khz(t->bit_clk_khz, true);
  sleep_ms(10);

  dvi0.timing = t;
  dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
  
  // Only set the GPIO base if we are using pin numbers above 31
  const bool high_gpio_base = 
    (dvi0.ser_cfg.pins_tmds[0] > 31) ||
    (dvi0.ser_cfg.pins_tmds[1] > 31) ||
    (dvi0.ser_cfg.pins_tmds[2] > 31) ||
    (dvi0.ser_cfg.pins_clk > 31) 
  ;

  if (high_gpio_base) {
    pio_set_gpio_base(DVI_DEFAULT_SERIAL_CONFIG.pio,16);
  }

  dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());
}

void ZxDviRenderLoopInit() {
  ZxDviRenderLoopInit_s(&DVI_TIMING);
}

void __not_in_flash_func(ZxDviRenderLoop)(
    ZxSpectrum &zxSpectrum,
    volatile uint &frames,
    bool &showMenu,
    volatile bool &toggleMenu,
    ZxSpectrumMenu& picoRootWin)
{
    dvi_register_irqs_this_core(&dvi0, DMA_IRQ_1);
    dvi_start(&dvi0);
    dvi_running = true;
    
    uint8_t* screenPtr;
    uint8_t* attrPtr;
    screenPtr = zxSpectrum.screenPtr();
    attrPtr = screenPtr + (32 * 24 * 8);

    uint y = 0;
    uint ys = 0;
    for(int i = 0; i < (DISPLAY_BLANK_LINES/2); ++i) {
      if (showMenu) {
        pcw_prepare_blankline_80(&dvi0, frames);
      }
      else {
        zx_prepare_hdmi_scanline(&dvi0, y, frames, screenPtr, attrPtr, 0);
      }
    }
    while(true) {
      const uint32_t fpf = zxSpectrum.flipsPerFrame();
      const bool ringoMode = fpf > 46 && fpf < 52;
      if (ringoMode) {
        screenPtr = zxSpectrum.memPtr(y & 4 ? 7 : 5);
        attrPtr = screenPtr + (32 * 24 * 8);
      }
#ifdef ZX_CZDIAG
      // CZD diag: sample the cubz() predicate and h_active on EVERY line,
      // branch taken or not (d + b below).
      zxCzDiagLine(zxSpectrum.cubz(), dvi0.timing->h_active_pixels);
#endif
  
      if (showMenu) {
        uint rs = pcw_prepare_scanline_80(&dvi0, y++, ys, frames);
        if (0 == (y & 7)) {
          ys += rs;
        }
      }
      else if (zxSpectrum.cobra80Char()) {
        zx_prepare_hdmi_scanline_cobra80(
          &dvi0,
          y,
          frames,
          zxSpectrum.cobraVram(),
          zxSpectrum.borderColour(y)
        );
        y++;
      }
      else if (zxSpectrum.cubz()) {
        // ICE Felix CUB-Z: 512x256 1bpp VRAM in _RAM[4], row remap +
        // SCROLL latch, bright green phosphor on the DVI/HDMI path.
#ifdef ZX_CZDIAG
        zxCzDiagHit(); // CZD diag (a): the cubz branch was actually taken
#endif
        zx_prepare_hdmi_scanline_cubz(
          &dvi0,
          y,
          zxSpectrum.cubzVram(),
          zxSpectrum.cubzScroll()
        );
        y++;
      }
      else {
        zx_prepare_hdmi_scanline(
          &dvi0, 
          y, 
          frames, 
          screenPtr, 
          attrPtr, 
          zxSpectrum.borderColour(y)
        );
        y++;
      }
      ZxRenderLoopCallbackLine(y);
      if (y == ZX_SPECTRUM_SCREEN_HEIGHT) {
        y = 0;
        ys = 0;
        for(int i = 0; i < DISPLAY_BLANK_LINES; ++i) {
          if (showMenu) {
            pcw_prepare_blankline_80(&dvi0, frames);
          }
          else {           
            zx_prepare_hdmi_scanline(&dvi0, 239, frames, screenPtr, attrPtr, 0);
          }
        }
       frames++;
#ifdef ZX_CZDIAG
        zxCzDiagFrameEnd(); // CZD diag (a): second latch on the frame wrap
#endif
        if (!ringoMode) {
          screenPtr = zxSpectrum.screenPtr();
          attrPtr = screenPtr + (32 * 24 * 8);
        }
        if (toggleMenu) {
          showMenu = !showMenu;
          toggleMenu = false;
          ZxRenderLoopCallbackMenu(showMenu);
        }
      }
    }
}
