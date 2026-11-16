// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "PicoCoreVoltage.h"
#include "hardware/pwm.h"

#include "ps2kbd.h"

// #include "ZxSpectrumKeyMatrix.h"


#include "PicoCharRendererVga.h"
#include "PicoWinHidKeyboard.h"
#include "PicoDisplay.h"
#include "PicoPen.h"
#include "PicoTextField.h"
#include "PicoWinHidKeyboard.h"

#include "ZxSpectrumFatSpiKiosk.h"
#include "ZxSpectrum.h"
#include "ZxSpectrumCf.h"
#include "ZxSpectrumHidKeyboard.h"
#include "ZxSpectrumDualJoystick.h"
#include "ZxSpectrumHidJoystick.h"
#include "ZxSpectrumHidMouse.h"

#include "bsp/board.h"
#include "tusb.h"
#include <pico/printf.h>
#include "SdCardFatFsSpi.h"
#include "QuickSave.h"
#include "ZxSpectrumFileLoop.h"
#include "PicoWinHidKeyboard.h"
#include "PicoDisplay.h"
#include "ZxSpectrumMenu.h"
#include "ZxSpectrumAudio.h"
#include "FatFsDirCache.h"
#include "ZxSpectrumDisplay.h"
#include "ZxSpectrumNespadJoystick.h"
#include "hid_app.h"
#include "ZxSpectrumFileSettings.h"
#include "ZxSpectrumAudioDriver.h"
#include "ZxSpectrumVideoDriver.h"
#ifdef ZX_VIDEO_AUTO_DETECT
#include "VideoAutoDetect.h"
#endif
#ifdef ZX_AUDIO_DVI_MIRROR
#include "ZxSpectrumAudioHdmi.h"
#endif

#ifndef VREG_VSEL
#define VREG_VSEL VREG_VOLTAGE_1_20
#endif

#define LED_PIN 25

static SdCardFatFsSpi sdCard0(0);

// ZX48 SuperSpec phase 2 (same as the VGA main): the machine's own
// CompactFlash in memory mode, backed by zxspectrum/cf/SUPERSPEC.CF on
// this SD card. Attached to the machine below; the CF ports only decode
// while the DOS bank 224 is latched (ZxSpectrum readIO/writeIO SuperSpec
// branches).
static ZxSpectrumCf superSpecCf(&sdCard0);

// ZX Spectrum emulator
static ZxSpectrumFatSpiKiosk zxSpectrumKisok(
  &sdCard0,
  "zxspectrum"
);
static ZxSpectrumFileLoop snapFileLoop;
static QuickSave quickSave;
static ZxSpectrumHidMouse mouse;
static ZxSpectrumNespadJoystick joystickNespad;
#ifdef NESPAD_ENABLE
static ZxSpectrumHidJoystick joystickHid;
static ZxSpectrumDualJoystick joystick(&joystickHid, &joystickNespad);
#else
static ZxSpectrumHidJoystick joystick;
#endif

static ZxSpectrumHidKeyboard keyboard1(
  &snapFileLoop,
  &quickSave,
  &joystick,
  &mouse
);
static ZxSpectrum zxSpectrum(
  &keyboard1,
  0,
  &joystick,
  &mouse
);
static ZxSpectrumFileSettings zxSpectrumSettings(
  &sdCard0,
  "zxspectrum", ".config"
);
static ZxSpectrumMenu picoRootWin(
  &sdCard0,
  &zxSpectrum,
  &zxSpectrumSettings
);
static PicoDisplay picoDisplay(
  pcw_screen(),
  &picoRootWin
);
static PicoWinHidKeyboard picoWinHidKeyboard(
  &picoDisplay
);

static bool showMenu = true;
static bool toggleMenu = false;

extern "C" void __not_in_flash_func(process_mouse_report)(hid_mouse_report_t const * report)
{
  // static hid_mouse_report_t prev_report = { 0 };

  // //------------- button state  -------------//
  // uint8_t button_changed_mask = report->buttons ^ prev_report.buttons;
  // if ( button_changed_mask & report->buttons)
  // {
  //   printf(" %c%c%c ",
  //      report->buttons & MOUSE_BUTTON_LEFT   ? 'L' : '-',
  //      report->buttons & MOUSE_BUTTON_MIDDLE ? 'M' : '-',
  //      report->buttons & MOUSE_BUTTON_RIGHT  ? 'R' : '-');
  // }

  // //------------- cursor movement -------------//
  // printf("(%d %d %d)\r\n", report->x, report->y, report->wheel);

  mouse.xDelta(report->x);
  mouse.yDelta(report->y);
  mouse.wDelta(report->wheel);
  mouse.setButtons(report->buttons);
}

extern "C"  void __not_in_flash_func(process_mouse_mount)(uint8_t dev_addr, uint8_t instance) {
//  printf("Mouse connected %d %d\n", dev_addr, instance);
  mouse.mount();
}

extern "C"  void __not_in_flash_func(process_mouse_unmount)(uint8_t dev_addr, uint8_t instance) {
//  printf("Mouse disonnected %d %d\n", dev_addr, instance);
  mouse.unmount();
}

void print(hid_keyboard_report_t const *report) {
  printf("HID key report modifiers %2.2X report ", report->modifier);
  for(int i = 0; i < 6; ++i) printf("%2.2X", report->keycode[i]);
  printf("\n");
}

extern "C"  void __not_in_flash_func(process_kbd_mount)(uint8_t dev_addr, uint8_t instance) {
  keyboard1.mount();
}

extern "C"  void __not_in_flash_func(process_kbd_unmount)(uint8_t dev_addr, uint8_t instance) {
  keyboard1.unmount();
}

extern "C"  void __not_in_flash_func(process_kbd_report)(hid_keyboard_report_t const *report, hid_keyboard_report_t const *prev_report) {
#if 0
  // Some help debugging keyboard input/drivers
  printf("PREV ");print(prev_report);
  printf("CURR ");print(report);
#endif

  int r;
  if (showMenu) {
    r = picoWinHidKeyboard.processHidReport(report, prev_report);
  }
  else {
    r = keyboard1.processHidReport(report, prev_report);
  }
  if (r == 1) {
    toggleMenu = true;
    picoRootWin.repaint();
    picoWinHidKeyboard.cancelRepeat();
  }
}

void __not_in_flash_func(process_joystick)() {
  int r;
  if (showMenu) {
      r = picoWinHidKeyboard.processJoystick(joystick.joy1());
  }
  else {
      r = picoWinHidKeyboard.processJoystickMenuEnter(joystick.joy1());
  }
  if (r)  {
    toggleMenu = true;
    picoRootWin.repaint();
    picoWinHidKeyboard.cancelRepeat();
  }
}

#if defined(USE_PS2_KBD)
static Ps2Kbd ps2kbd(
  pio1,
  PS2KBD_GPIO,
  process_kbd_report
);
#endif

static volatile uint _frames = 0;

void __not_in_flash_func(ZxRenderLoopCallbackLine)(int32_t y) {
}

void __not_in_flash_func(ZxRenderLoopCallbackMenu)(bool state) {
}

void __not_in_flash_func(core1_main)() {

  ZxSpectrumVideoLoop(
    zxSpectrum,
    _frames,
    showMenu,
    toggleMenu,
    picoRootWin
  );

  while (1) 
    __wfi();
  __builtin_unreachable();
}

#define CPU_STEP_LOOP 100

void __not_in_flash_func(main_loop)(){

  unsigned int lastInterruptFrame = _frames;

  //Main Loop
  uint frames = 0;

  while(1){

    tuh_task();
#ifdef USE_PS2_KBD
    ps2kbd.tick();
#endif

//    hid_keyboard_report_t const *curr;
//    hid_keyboard_report_t const *prev;
//    zx_keyscan_get_hid_reports(&curr, &prev);
//    process_picomputer_kbd_report(curr, prev);
    process_joystick();

    if (!showMenu) {
      for (int i = 1; i < CPU_STEP_LOOP; ++i) {
        if (lastInterruptFrame != _frames) {
          lastInterruptFrame = _frames;
          zxSpectrum.vsync();
        }
        zxSpectrum.step();
      }
    }
    else {
      picoWinHidKeyboard.processKeyRepeat();
      if (frames != _frames) {
        frames = _frames;
        // Menu mode: force a full repaint every frame so live readouts
        // (e.g. the row-7 joystick probe in the menu header) update
        // without any key/joystick event.
        picoRootWin.repaint();
        picoDisplay.refresh();
      }
    }
  }
  __builtin_unreachable();
}

int main(){
//  gpio_init(LED_PIN);
//  gpio_set_dir(LED_PIN, GPIO_OUT);

  pico_set_core_voltage();

  // Try to use the general settings for video and audio defaults
  // This will start up the SD card before the system frequency is decided
  ZxSpectrumSettingValues settings;
  zxSpectrumSettings.load(&settings);
  setZxSpectrumVideoDriver((zx_spectrum_video_driver_enum_t)settings.videoDriverDefault);
#ifdef ZX_VIDEO_AUTO_DETECT
  // Boot-time auto-detect on the shared GPIO12-19 block: HDMI/DVI sink
  // termination pulls the TMDS pairs high, a VGA monitor does not.
  // Exactly one pipeline is started below (via ZxSpectrumVideoInit).
  const bool hdmiSink = zxVideoAutoDetect();
  setZxSpectrumVideoDriver(hdmiSink
    ? zx_spectrum_video_driver_dvi_index
    : zx_spectrum_video_driver_vga_index);
#endif

  // Not that the following sets the system clock
  ZxSpectrumVideoInit();

#ifdef USE_STDIO
  //Initialise I/O
  stdio_init_all();
#endif

  picoRootWin.refresh([&]() { picoDisplay.refresh(); });
  picoRootWin.snapLoaded([&](const char *name) {
      toggleMenu = showMenu;
    }
  );
  // TZX tape option handlers
  zxSpectrum.tzxOptionHandlers(
    [&]() { // Clear options
      picoRootWin.clearTzxOptions();
    },
    [&](const char *s) { // Add option
      picoRootWin.addTzxOption(s);
    },
    [&]() { // Show options
      picoRootWin.showTzxOptions();
      toggleMenu = !showMenu;
    }
  );
  picoRootWin.tzxOption(
    [&](uint32_t option) {
      zxSpectrum.tzxOption(option);
      toggleMenu = showMenu;
    }
  );
  snapFileLoop.set(&picoRootWin);
  quickSave.set(&picoRootWin);

  tuh_hid_app_startup();

#ifdef NESPAD_ENABLE
  joystickNespad.init(); // pio1, SM ?
#endif
#ifdef USE_PS2_KBD
  ps2kbd.init_gpio(); // pio1, SM ?
#endif

  // Setup the default audio driver. This target always drives the I2S DAC;
  // when the HDMI sink was detected the same sample stream is additionally
  // mirrored into the DVI audio ring from the I2S sample handler
  // (ZX_AUDIO_DVI_MIRROR). zxSpectrumAudioInit initialises every compiled-in
  // audio driver, including the DVI ring (hdmi_audio_init is a no-op unless
  // the DVI pipeline was started above).
  zxSpectrum.setAudioDriver(zxSpectrumAudioInit(zx_spectrum_audio_driver_i2s_index));

  keyboard1.setZxSpectrum(&zxSpectrum);
//  keyboard2.setZxSpectrum(&zxSpectrum);

  // Attach the SuperSpec CF emulation (image opened lazily at the machine's
  // first CF port access; missing file = no-media "sector error", natural).
  zxSpectrum.setCf(&superSpecCf);

  // Initialise the menu renderer
  pcw_init_renderer();

  // Initialise the keyboard scan
//  zx_keyscan_init();

  sleep_ms(10);

  multicore_launch_core1(core1_main);

  picoRootWin.showMessage([=](PicoPen *pen) {
    pen->printAtF(3, 1, false, "Reading from SD card...");
  });

  picoDisplay.refresh();

  if (sdCard0.mount()) {

    // Create folders on the SD card if they are missing
    picoRootWin.initialise();

    // Load quick save slot 1 if present
    quickSave.load(&zxSpectrum, 0);

    // See if the board is in kiosk mode
    bool isKiosk = zxSpectrumKisok.isKiosk();
    keyboard1.setKiosk(isKiosk);
//    keyboard2.setKiosk(isKiosk);
  }

  showMenu = false;
  picoRootWin.removeMessage();

  main_loop();
}

