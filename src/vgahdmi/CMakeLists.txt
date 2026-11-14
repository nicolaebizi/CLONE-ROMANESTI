# SPDX-License-Identifier: MIT
# Copyright (c) 2026 fruit-bat
########################################################################
# ZX-MURMULATOR2 SpeccyP - dual VGA / HDMI with boot-time auto-detect
#
# Single RP2350 (pico2) target: at boot the shared GPIO12-19 block is
# probed (VideoAutoDetect) and exactly one video pipeline is started:
#   - HDMI/DVI sink detected  -> libdvi DVI 640x480p60 (TMDS on 12-19)
#   - VGA monitor assumed     -> pico_scanvideo_dpi 640x480x60 (RGB 222
#                                + VSYNC on the same GPIO12-19)
# Audio is always the I2S DAC (GPIO9 data, 10 BCLK, 11 LRCLK); when the
# HDMI sink was detected the same sample stream is additionally mirrored
# into the DVI audio ring (ZX_AUDIO_DVI_MIRROR).
########################################################################

# The menu library normally comes in three mutually-exclusive flavours
# (pico_dvi_menu / pico_scanvideo_menu / pico_vga_menu) which all share the
# same pico_menu_common sources, so linking two of them would duplicate
# every common symbol. For this target we build a combined INTERFACE
# library with the common sources compiled once plus both renderers.
set(PICO_DVI_MENU_DIR ${PICO_FOLDER}/pico-dvi-menu)
set(pico_menu_common
	${PICO_DVI_MENU_DIR}/src/PicoRectXYWH.cpp
	${PICO_DVI_MENU_DIR}/src/PicoRectXYXY.cpp
	${PICO_DVI_MENU_DIR}/src/PicoCharScreen.cpp
	${PICO_DVI_MENU_DIR}/src/PicoClippedScreen.cpp
	${PICO_DVI_MENU_DIR}/src/PicoWinHidKeyboard.cpp
	${PICO_DVI_MENU_DIR}/src/PicoPen.cpp
	${PICO_DVI_MENU_DIR}/src/PicoWin.cpp
	${PICO_DVI_MENU_DIR}/src/PicoDisplay.cpp
	${PICO_DVI_MENU_DIR}/src/PicoWinBlock.cpp
	${PICO_DVI_MENU_DIR}/src/PicoSelect.cpp
	${PICO_DVI_MENU_DIR}/src/PicoOption.cpp
	${PICO_DVI_MENU_DIR}/src/PicoOptionText.cpp
	${PICO_DVI_MENU_DIR}/src/PicoQuickKey.cpp
	${PICO_DVI_MENU_DIR}/src/PicoQuickKeyAscii.cpp
	${PICO_DVI_MENU_DIR}/src/PicoWiz.cpp
	${PICO_DVI_MENU_DIR}/src/PicoWizUtils.cpp
	${PICO_DVI_MENU_DIR}/src/PicoWizExplorer.cpp
	${PICO_DVI_MENU_DIR}/src/PicoTextField.cpp
	${PICO_DVI_MENU_DIR}/src/PicoExplorer.cpp
	${PICO_DVI_MENU_DIR}/src/PicoStaticScreen.cpp
	${PICO_DVI_MENU_DIR}/src/PicoSlider.cpp
)

add_library(pico_dual_menu INTERFACE)

target_sources(pico_dual_menu INTERFACE
	${pico_menu_common}
	${PICO_DVI_MENU_DIR}/src/PicoCharRenderer.cpp       # DVI/HDMI menu renderer
	${PICO_DVI_MENU_DIR}/src/PicoCharRendererScanvideo.cpp # scanvideo menu renderer
)

target_include_directories(pico_dual_menu INTERFACE
	${PICO_DVI_MENU_DIR}/src
)

set(zxspectrum_vgahdmi_auto_src
  main_auto.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../VideoAutoDetect.cpp
)

add_executable(ZX-MURMULATOR2_VGA_HDMI_AUTO_640x480x60Hz
  ${zxspectrum_common_src}
  ${zxspectrum_rgb_src}
  ${zxspectrum_scanvideo_src}
  ${zxspectrum_dvi_src}
  ${zxspectrum_vgahdmi_auto_src}
)

target_link_libraries(ZX-MURMULATOR2_VGA_HDMI_AUTO_640x480x60Hz
  ${zxspectrum_common_libs}
  pico_emu_utils
  libdvi
  pico_scanvideo_dpi
  pico_dual_menu
  tinyusb_host
  tinyusb_board
  hardware_pio
  hardware_pwm
)

zxspectrum_generate_pio_headers(ZX-MURMULATOR2_VGA_HDMI_AUTO_640x480x60Hz)

pico_enable_stdio_usb(ZX-MURMULATOR2_VGA_HDMI_AUTO_640x480x60Hz 0)
pico_enable_stdio_uart(ZX-MURMULATOR2_VGA_HDMI_AUTO_640x480x60Hz 1)

set(zxspectrum_vgahdmi_auto_defines
  # ---- MURMULATOR2 board (same as the existing M2 HDMI/VGA targets) ----
  ZX_PLATFORM="MURMULATOR2"
  DVI_CUSTOM_CONFIG
  DVI_DEFAULT_SERIAL_CONFIG=murmulator2_hdmi_cfg
  PICO_CORE1_STACK_SIZE=0x400
  DVI_VERTICAL_REPEAT=2
  DVI_N_TMDS_BUFFERS=3
  DVI_1BPP_BIT_REVERSE=1
  # Tell FATFS we want to find files
  FF_USE_FIND=1
  # SD card on SPI0
  SDCARD_PIN_SPI0_CS=5
  SDCARD_PIN_SPI0_SCK=6
  SDCARD_PIN_SPI0_MOSI=7
  SDCARD_PIN_SPI0_MISO=4
  # Alternate AY volume map, tape input, PS/2 keyboard
  AY8912_ALT_VOL_MAP
  EAR_PIN=22
  USE_PS2_KBD
  PS2KBD_CLK2_DAT
  # Dual NES joypads
  NESPAD_DATA_PIN_BASE=26
  NESPAD_CLK_PIN_BASE=20
  NESPAD_PIO=pio1
  NESPAD_IRQ=PIO1_IRQ_0
  NESPAD_ENABLE
  # ---- I2S DAC, always on (LRCLK = BCLK + 1 = GPIO11) ----
  PICO_AUDIO_I2S=1
  PICO_AUDIO_I2S_PIO=pio1
  PICO_AUDIO_I2S_PIO_FUNC=GPIO_FUNC_PIO1
  PICO_AUDIO_I2S_DATA=9
  PICO_AUDIO_I2S_BCLK=10
  # ---- DVI/HDMI 640x480p60 pipeline ----
  DVI_TIMING=dvi_timing_640x480p_60hz
  # Menu screen resolution
  PCS_COLS=80
  PCS_ROWS=30
  # Display resolution
  DISPLAY_WIDTH_PIXELS=640
  DISPLAY_HEIGHT_PIXELS=480
  ZX_SPECTRUM_INT_SRC=SyncToDisplay
  PICO_PWM_AUDIO_FREQ=42000
  # ---- VGA 640x480x60 pipeline (shared GPIO12-19, RGB 222 + VSYNC) ----
  VGA_MODE=vga_mode_640x240_60
  VGA_ENC_RGB_222
  VGA_VSYNC
  PICO_SCANVIDEO_COLOR_PIN_COUNT=6u
  PICO_SCANVIDEO_DPI_PIXEL_RCOUNT=2u
  PICO_SCANVIDEO_DPI_PIXEL_GCOUNT=2u
  PICO_SCANVIDEO_DPI_PIXEL_BCOUNT=3u
  PICO_SCANVIDEO_DPI_PIXEL_RSHIFT=4u
  PICO_SCANVIDEO_DPI_PIXEL_GSHIFT=2u
  PICO_SCANVIDEO_DPI_PIXEL_BSHIFT=0u
  PICO_SCANVIDEO_COLOR_PIN_BASE=12u
  PICO_SCANVIDEO_MAX_SCANLINE_BUFFER_WORDS=700
  # ---- Core voltage 1.30V ----
  VREG_VSEL=VREG_VOLTAGE_1_30
  # ---- Dual audio: I2S always + DVI audio ring when HDMI is detected ----
  PICO_HDMI_AUDIO
  ZX_AUDIO_DVI_MIRROR
  # ---- Boot-time VGA/HDMI auto-detect ----
  ZX_VIDEO_AUTO_DETECT
)

target_compile_definitions(ZX-MURMULATOR2_VGA_HDMI_AUTO_640x480x60Hz PRIVATE
  ${zxspectrum_vgahdmi_auto_defines}
)

# SDK >= 2.x board headers (pico2) contain pico_board_cmake_set(...) /
# pico_board_cmake_set_default(...) marker macros which pico.h defines as
# no-ops. libdvi's tmds_encode.S includes pico/config.h directly (bypassing
# pico.h), so when assembling for rp2350 the markers leak into the
# assembler. Define the markers empty for this target only.
target_compile_options(ZX-MURMULATOR2_VGA_HDMI_AUTO_640x480x60Hz PRIVATE
  "-Dpico_board_cmake_set(x,y)="
  "-Dpico_board_cmake_set_default(x,y)="
)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(ZX-MURMULATOR2_VGA_HDMI_AUTO_640x480x60Hz)
