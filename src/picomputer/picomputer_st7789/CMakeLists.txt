# SPDX-License-Identifier: MIT
# Copyright (c) 2026 fruit-bat
include_directories(${picomputer_common_inc})

add_executable(ZxSpectrumPicomputerMax
  ${zxspectrum_common_src}
  ${picomputer_common_src}
  ${zxspectrum_st7789_lcd_src}
  main.cpp
  st7789_lcd.cpp
  pzx_prepare_rgb444_scanline.cpp
)

zxspectrum_generate_pio_headers(ZxSpectrumPicomputerMax)

target_compile_definitions(ZxSpectrumPicomputerMax PRIVATE
  ${picomputer_common_defines}
  # Set the resolution of the menu screen
  PCS_COLS=40
  PCS_ROWS=30
  # Set the size of the menus
  SZ_WIZ_ML=0
  SZ_WIZ_CW1=13
  SZ_WIZ_CW2=18
  SZ_FRAME_COLS=40
  SZ_FRAME_ROWS=30
  SZ_FRAME_X=0
  SZ_FRAME_Y=0
  SZ_MENU_SEP=1
  SZ_FILE_SEP=1
  SZ_FILE_ROWS=18
  PICO_PIO_PWM_AUDIO
  PICO_AUDIO_PWM_PIO=pio1  
  SPK_PIN=0
  PICOMPUTER_MAX
  LCD_MIRROR_X
  PICO_ST7789_LCD
  USE_KEY_MATRIX
)

target_link_libraries(ZxSpectrumPicomputerMax
  ${zxspectrum_common_libs}
  pico_emu_utils
  pico_st7789_menu
  tinyusb_host
  tinyusb_board
  hardware_pio
  hardware_pwm
)

pico_enable_stdio_usb(ZxSpectrumPicomputerMax 0)
pico_enable_stdio_uart(ZxSpectrumPicomputerMax 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(ZxSpectrumPicomputerMax)

add_executable(ZxSpectrumPicomputerZX
  ${zxspectrum_common_src}
  ${picomputer_common_src}
  ${zxspectrum_st7789_lcd_src}
	main.cpp
  st7789_lcd.cpp
  pzx_prepare_rgb444_scanline.cpp
)

zxspectrum_generate_pio_headers(ZxSpectrumPicomputerZX)

target_compile_definitions(ZxSpectrumPicomputerZX PRIVATE
  ${picomputer_common_defines}
  # Set the resolution of the menu screen
  PCS_COLS=40
  PCS_ROWS=30
  # Set the size of the menus
  SZ_WIZ_ML=0
  SZ_WIZ_CW1=13
  SZ_WIZ_CW2=18
  LCD_SZ_FRAME_COLS=34
  LCD_SZ_FRAME_ROWS=23
  LCD_SZ_FRAME_X=3
  LCD_SZ_FRAME_Y=3
  SZ_MENU_SEP=1
  SZ_FILE_SEP=1
  SZ_FILE_ROWS=11
  PICO_PIO_PWM_AUDIO
  PICO_AUDIO_PWM_PIO=pio1  
  SPK_PIN=0
  PICOMPUTER_ZX
  LCD_MIRROR_X
  PICO_ST7789_LCD
  USE_KEY_MATRIX
)

target_link_libraries(ZxSpectrumPicomputerZX
  ${zxspectrum_common_libs}
  pico_emu_utils
  pico_st7789_menu
  tinyusb_host
  tinyusb_board
  hardware_pio
  hardware_pwm
)

pico_enable_stdio_usb(ZxSpectrumPicomputerZX 0)
pico_enable_stdio_uart(ZxSpectrumPicomputerZX 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(ZxSpectrumPicomputerZX)

set(picomputer_zx_lcd_src
  ${zxspectrum_common_src}
  ${picomputer_common_src}
  ${zxspectrum_rgb_src}
  ${zxspectrum_scanvideo_src}
  ${zxspectrum_st7789_lcd_src}
  main.cpp
)

set(zxspectrum_zx_lcd_640x480x60Hz_defines
  # Set the resolution of the menu screen
  PCS_COLS=80
  PCS_ROWS=30
  # Set the VGA timing
  VGA_MODE=vga_mode_640x240_60
  # The display resolution
  DISPLAY_WIDTH_PIXELS=640
  DISPLAY_HEIGHT_PIXELS=480
  # The default interrupt source
  ZX_SPECTRUM_INT_SRC=SyncToCpu
  # TODO work what this is
  PICO_SCANVIDEO_MAX_SCANLINE_BUFFER_WORDS=700
  # Speaker pin audio (@250MHz 250000000 / (1000 * 3 * 2))
  PICO_PWM_AUDIO_FREQ=41666
)

set(zxspectrum_zx_lcd_720x576x50Hz_defines
  # Set the resolution of the menu screen
  PCS_COLS=90
  PCS_ROWS=30
  SZ_FRAME_X=5
  # Set the VGA timing
  VGA_MODE=vga_mode_720x288_50
  # The display resolution
  DISPLAY_WIDTH_PIXELS=720
  DISPLAY_HEIGHT_PIXELS=576
  # The default interrupt source
  ZX_SPECTRUM_INT_SRC=SyncToDisplay
  # TODO work what this is
  PICO_SCANVIDEO_MAX_SCANLINE_BUFFER_WORDS=800
  # Speaker pin audio (@270MHz 270000000 / (1000 * 3 * 2))
  PICO_PWM_AUDIO_FREQ=45000
)

set(picomputer_zx_lcd_common_defines PRIVATE
  ${picomputer_common_defines}
  PICO_PIO_PWM_AUDIO
  PICO_AUDIO_PWM_PIO=pio1
  SPK_PIN=7
  PICOMPUTER_PICOZX
  PICOMPUTER_PICOZX_LCD
  INITIAL_VOL=0x30
  PICO_ST7789_LCD
  USE_KEY_MATRIX
  LCD_NO_BL
  LCD_PIN_CLK=2
  LCD_PIN_DC=6
  LCD_PIN_CS=5
  LCD_PIN_DIN=3
  VGA_ENC_RGB_222
  PICO_SCANVIDEO_COLOR_PIN_COUNT=6u
  PICO_SCANVIDEO_DPI_PIXEL_RCOUNT=2u
  PICO_SCANVIDEO_DPI_PIXEL_GCOUNT=2u
  PICO_SCANVIDEO_DPI_PIXEL_BCOUNT=3u
  PICO_SCANVIDEO_DPI_PIXEL_RSHIFT=4u
  PICO_SCANVIDEO_DPI_PIXEL_GSHIFT=2u
  PICO_SCANVIDEO_DPI_PIXEL_BSHIFT=0u
  PICO_SCANVIDEO_COLOR_PIN_BASE=0u
)

set(zxspectrum_zx_lcd_common_libs
  ${zxspectrum_common_libs}
  pico_emu_utils
  pico_st7789_menu
  pico_scanvideo_dpi
  pico_scanvideo_menu
  tinyusb_host
  tinyusb_board
  hardware_pio
  hardware_pwm
  hardware_adc
)

foreach(target
  ZxSpectrumPicomputerZxLcd_640x480x60Hz
  ZxSpectrumPicomputerZxLcd_720x576x50Hz
  ZxSpectrumPicomputerZxInverseLcd_640x480x60Hz
  ZxSpectrumPicomputerZxInverseLcd_720x576x50Hz
  ZxSpectrumPicomputerZxILI9341Lcd_720x576x50Hz
  ZxSpectrumPicomputerZxILI9341Lcd_640x480x60Hz
  ZxSpectrumPicomputerZx2Lcd_720x576x50Hz
  ZxSpectrumPicomputerZx2Lcd_640x480x60Hz
)
  add_executable(${target}
    ${picomputer_zx_lcd_src}
  )

  zxspectrum_generate_pio_headers(${target})

  target_link_libraries(${target}
    ${zxspectrum_zx_lcd_common_libs}
  )

  pico_enable_stdio_usb(${target} 0)
  pico_enable_stdio_uart(${target} 0)

  # create map/bin/hex file etc.
  pico_add_extra_outputs(${target})

endforeach()

target_compile_definitions(ZxSpectrumPicomputerZxLcd_720x576x50Hz PRIVATE
  ${picomputer_zx_lcd_common_defines}
  ${zxspectrum_zx_lcd_720x576x50Hz_defines}
  LCD_MIRROR_X
)

target_compile_definitions(ZxSpectrumPicomputerZxLcd_640x480x60Hz PRIVATE
  ${picomputer_zx_lcd_common_defines}
  ${zxspectrum_zx_lcd_640x480x60Hz_defines}
  LCD_MIRROR_X
)

target_compile_definitions(ZxSpectrumPicomputerZxInverseLcd_720x576x50Hz PRIVATE
  ${picomputer_zx_lcd_common_defines}
  ${zxspectrum_zx_lcd_720x576x50Hz_defines}
  LCD_MIRROR_X
  LCD_INVERSE
)

target_compile_definitions(ZxSpectrumPicomputerZxInverseLcd_640x480x60Hz PRIVATE
  ${picomputer_zx_lcd_common_defines}
  ${zxspectrum_zx_lcd_640x480x60Hz_defines}
  LCD_MIRROR_X
  LCD_INVERSE
)

target_compile_definitions(ZxSpectrumPicomputerZxILI9341Lcd_720x576x50Hz PRIVATE
  ${picomputer_zx_lcd_common_defines}
  ${zxspectrum_zx_lcd_720x576x50Hz_defines}
  LCD_INVERSE
  LCD_RGB
)

target_compile_definitions(ZxSpectrumPicomputerZxILI9341Lcd_640x480x60Hz PRIVATE
  ${picomputer_zx_lcd_common_defines}
  ${zxspectrum_zx_lcd_640x480x60Hz_defines}
  LCD_INVERSE
  LCD_RGB
)

target_compile_definitions(ZxSpectrumPicomputerZx2Lcd_720x576x50Hz PRIVATE
  ${picomputer_zx_lcd_common_defines}
  ${zxspectrum_zx_lcd_720x576x50Hz_defines}
  LCD_MIRROR_X
  REAL_ZXKEYBOARD
  REAL_ZXKEYBOARD_BOB
  HID_KEY_MOD_SAVE=2
  DEFAULT_VIDEO_DRIVER=zx_spectrum_video_driver_vga_index
)

target_compile_definitions(ZxSpectrumPicomputerZx2Lcd_640x480x60Hz PRIVATE
  ${picomputer_zx_lcd_common_defines}
  ${zxspectrum_zx_lcd_640x480x60Hz_defines}
  LCD_MIRROR_X
  REAL_ZXKEYBOARD
  REAL_ZXKEYBOARD_BOB
  HID_KEY_MOD_SAVE=2
  DEFAULT_VIDEO_DRIVER=zx_spectrum_video_driver_vga_index
)

# ZX MURMULATOR board with ILI9341 SPI TFT (ST7789 driver path) + I2S audio + PS/2 keyboard
add_executable(ZxSpectrumMurmulatorTftIli9341I2sPs2
  ${zxspectrum_common_src}
  ${picomputer_common_src}
  ${zxspectrum_st7789_lcd_src}
  main.cpp
  st7789_lcd.cpp
  pzx_prepare_rgb444_scanline.cpp
)

zxspectrum_generate_pio_headers(ZxSpectrumMurmulatorTftIli9341I2sPs2)

target_compile_definitions(ZxSpectrumMurmulatorTftIli9341I2sPs2 PRIVATE
  # ILI9341 TFT LCD
  PICO_ST7789_LCD
  LCD_PIN_CS=6
  LCD_PIN_DC=10
  LCD_PIN_DIN=12
  LCD_PIN_CLK=13
  LCD_PIN_BL=9
  LCD_PIN_RESET=8
  LCD_INVERSE
  LCD_RGB
  # ZX MURMULATOR peripherals, copied from ZX-MURMULATOR_VGA_I2S
  # target in src/vga/CMakeLists.txt (VGA/scanvideo and NESPAD defines
  # omitted - not applicable to this LCD build)
  ZX_PLATFORM="MURMULATOR"
  # Configure the SPI sd card to use SPI0
  SDCARD_PIN_SPI0_CS=5
  SDCARD_PIN_SPI0_SCK=2
  SDCARD_PIN_SPI0_MOSI=3
  SDCARD_PIN_SPI0_MISO=4
  # Use the alternate volume map
  AY8912_ALT_VOL_MAP
  # Tape audio in
  EAR_PIN=22
  # Enable the PS/2 keyboard
  USE_PS2_KBD
  PS2KBD_CLK0_DAT
  # Configure the I2S audio, pio1 SM ?
  PICO_AUDIO_I2S=1
  PICO_AUDIO_I2S_PIO=pio1
  PICO_AUDIO_I2S_PIO_FUNC=GPIO_FUNC_PIO1
  PICO_AUDIO_I2S_DATA=26
  PICO_AUDIO_I2S_BCLK=27
  # Keyboard variant for compiling pzx_keyscan.cpp (pulled in via
  # picomputer_common_src). NOTE: USE_KEY_MATRIX is intentionally NOT
  # defined - like the ZX-MURMULATOR VGA target, keyboard input comes
  # from PS/2 through the TinyUSB HID stack, so the local GPIO key
  # matrix is never initialised and stays dormant.
  PICOMPUTER_MAX
  # NOTE: PICOMPUTER_PICOZX_LCD is intentionally NOT defined here. On
  # Murmulator M1 GPIO4 is SD SPI0 MISO; the PicoZX backlight hack in
  # ZxSt7789LcdRenderLoop.cpp (gpio_put(4,1)) must never compile in.
  # Backlight is driven by st7789_lcd.cpp via LCD_PIN_BL=9 instead.
  # Core voltage default overridden to avoid brownout on this board.
  VREG_VSEL=VREG_VOLTAGE_1_30
)

target_link_libraries(ZxSpectrumMurmulatorTftIli9341I2sPs2
  ${zxspectrum_common_libs}
  pico_emu_utils
  pico_st7789_menu
  tinyusb_host
  tinyusb_board
  hardware_pio
  hardware_pwm
)

pico_enable_stdio_usb(ZxSpectrumMurmulatorTftIli9341I2sPs2 0)
pico_enable_stdio_uart(ZxSpectrumMurmulatorTftIli9341I2sPs2 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(ZxSpectrumMurmulatorTftIli9341I2sPs2)

# ZX MURMULATOR2 board with ILI9341 SPI TFT (ST7789 driver path) + I2S audio + PS/2 keyboard
add_executable(ZxSpectrumMurmulator2TftIli9341I2sPs2
  ${zxspectrum_common_src}
  ${picomputer_common_src}
  ${zxspectrum_st7789_lcd_src}
  main.cpp
  st7789_lcd.cpp
  pzx_prepare_rgb444_scanline.cpp
)

zxspectrum_generate_pio_headers(ZxSpectrumMurmulator2TftIli9341I2sPs2)

target_compile_definitions(ZxSpectrumMurmulator2TftIli9341I2sPs2 PRIVATE
  # ILI9341 TFT LCD - REAL SpeccyP GSP M2 HW102 (MURMULATOR, xtremespb/billgilbert7000)
  # TFT: CS=12 DC=16 DIN(TFT_SDA)=18 CLK(TFT_SCL)=19 BL=15 RST=14
  PICO_ST7789_LCD
  LCD_PIN_CS=12
  LCD_PIN_DC=16
  LCD_PIN_DIN=18
  LCD_PIN_CLK=19
  LCD_PIN_BL=15
  LCD_PIN_RESET=14
  LCD_INVERSE
  LCD_RGB
  # ZX MURMULATOR2 peripherals, copied from ZX-MURMULATOR2_VGA_I2S_AUDIO
  # targets in src/vga/CMakeLists.txt (VGA/scanvideo and NESPAD defines
  # omitted - not applicable to this LCD build)
  ZX_PLATFORM="MURMULATOR2"
  # Configure the SPI sd card to use SPI0
  SDCARD_PIN_SPI0_CS=5
  SDCARD_PIN_SPI0_SCK=6
  SDCARD_PIN_SPI0_MOSI=7
  SDCARD_PIN_SPI0_MISO=4
  # Use the alternate volume map
  AY8912_ALT_VOL_MAP
  # Tape audio in
  EAR_PIN=22
  # Enable the PS/2 keyboard
  # VERIFIED in pico-emu-utils/src/ps2kbd.h: PS2KBD_CLK2_DAT =>
  # PS2KBD_GPIO=2 (CLK), PS2KBD_GPIO_DAT_OFFSET=1 -> DATA=GPIO3.
  USE_PS2_KBD
  PS2KBD_CLK2_DAT
  # Pins for dual NES joypads on Gamepad 1, same as ZX-MURMULATOR2_VGA target
  # (src/vga/CMakeLists.txt): CLK=20, DATA=26, pio1, PIO1_IRQ_0.
  NESPAD_DATA_PIN_BASE=26
  NESPAD_CLK_PIN_BASE=20
  NESPAD_PIO=pio1
  NESPAD_IRQ=PIO1_IRQ_0
  NESPAD_ENABLE
  # Configure the I2S audio, pio1 SM ?
  # NOTE: no PICO_AUDIO_I2S_SCLK macro exists in this code path. The LR-clock
  # (word select, TDA_LCK=GPIO11) is DERIVED as PICO_AUDIO_I2S_BCLK+1 in
  # ZxSpectrumAudioI2s.cpp (gpio_set_function(PICO_AUDIO_I2S_BCLK + 1, ...)),
  # so BCLK=10 automatically yields SCLK=11.
  PICO_AUDIO_I2S=1
  PICO_AUDIO_I2S_PIO=pio1
  PICO_AUDIO_I2S_PIO_FUNC=GPIO_FUNC_PIO1
  PICO_AUDIO_I2S_DATA=9
  PICO_AUDIO_I2S_BCLK=10
  # Keyboard variant for compiling pzx_keyscan.cpp (pulled in via
  # picomputer_common_src). NOTE: USE_KEY_MATRIX is intentionally NOT
  # defined - keyboard input comes from PS/2 through the TinyUSB HID
  # stack, so the local GPIO key matrix is never initialised and stays
  # dormant.
  PICOMPUTER_MAX
  # NOTE: PICOMPUTER_PICOZX_LCD is intentionally NOT defined here so the
  # PicoZX backlight hack (gpio_put(4,1)) never compiles in; backlight is
  # driven by st7789_lcd.cpp via LCD_PIN_BL=15 instead.
  # Core voltage default overridden to avoid brownout on this board.
  VREG_VSEL=VREG_VOLTAGE_1_30
)

target_link_libraries(ZxSpectrumMurmulator2TftIli9341I2sPs2
  ${zxspectrum_common_libs}
  pico_emu_utils
  pico_st7789_menu
  tinyusb_host
  tinyusb_board
  hardware_pio
  hardware_pwm
)

pico_enable_stdio_usb(ZxSpectrumMurmulator2TftIli9341I2sPs2 0)
pico_enable_stdio_uart(ZxSpectrumMurmulator2TftIli9341I2sPs2 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(ZxSpectrumMurmulator2TftIli9341I2sPs2)
