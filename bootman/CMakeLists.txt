# BOOTMAN - standalone boot manager
#
# Builds a small permanent firmware flashed on the board flash slot 0
# (0x10000000). On every boot it shows a browser of the SD image dir; the
# chosen image is programmed into slot 0 and the board reboots.
#
# Variants (v0.2):
#   RP2350 VGA  (default): PICO_BOARD=pico2, VIDEO_TYPE=VGA
#                          -> bootman-murmulator-RP2350-VGA-v0.2
#   RP2350 HDMI          : PICO_BOARD=pico2, VIDEO_TYPE=HDMI (frank HDMI
#                          driver, video task + DMA IRQ on core1)
#                          -> bootman-murmulator-v0.2-HDMI
#   RP2040 VGA           : PICO_BOARD=pico -DPICO_PLATFORM=rp2040,
#                          VIDEO_TYPE=VGA (PIO VGA driver works as-is on
#                          RP2040: plain PIO + DMA, no RP2350-only feature)
#                          -> bootman-murmulator-v0.2-RP2040
#
# Usage:
#   cmake -DPICO_SDK_PATH=/Users/nicolaemuntean/pico-sdk \
#         [-DPICO_BOARD=pico2|-DPICO_BOARD=pico -DPICO_PLATFORM=rp2040] \
#         [-DVIDEO_TYPE=VGA|HDMI] [-DCPU_SPEED=<MHz>] ..
#   make -j8
#
cmake_minimum_required(VERSION 3.13)

# Board: pico2 (RP2350, default) or pico (RP2040). A -DPICO_BOARD=... from
# the command line wins over the default (CACHE set does not override).
set(PICO_BOARD "pico2" CACHE STRING "Board type: pico2 (RP2350) or pico (RP2040)")
set(VIDEO_TYPE "VGA" CACHE STRING "Video output: VGA (RP2350+RP2040) or HDMI (RP2350 only)")

# Platform detection (explicit -DPICO_PLATFORM=rp2040 also wins)
if(NOT PICO_PLATFORM)
    if(PICO_BOARD STREQUAL "pico")
        set(PLATFORM rp2040)
    else()
        set(PLATFORM rp2350)
    endif()
else()
    set(PLATFORM ${PICO_PLATFORM})
endif()

if(PLATFORM STREQUAL "rp2040")
    if(VIDEO_TYPE STREQUAL "HDMI")
        message(FATAL_ERROR "bootman: HDMI needs RP2350 (PIO1 differential output); use VIDEO_TYPE=VGA on RP2040")
    endif()
    set(BOOTMAN_EXEC bootman-murmulator-v0.2-RP2040)
    set(BOOTMAN_DRIVER_VIDEO vga_bootman.c)
else()
    if(NOT VIDEO_TYPE STREQUAL "VGA" AND NOT VIDEO_TYPE STREQUAL "HDMI")
        message(FATAL_ERROR "bootman: VIDEO_TYPE must be VGA or HDMI")
    endif()
    if(VIDEO_TYPE STREQUAL "HDMI")
        set(BOOTMAN_EXEC bootman-murmulator-v0.2-HDMI)
        set(BOOTMAN_DRIVER_VIDEO HDMI.c)
    else()
        set(BOOTMAN_EXEC bootman-murmulator-RP2350-VGA-v0.2)
        set(BOOTMAN_DRIVER_VIDEO vga_bootman.c)
    endif()
endif()

# Clock defaults: 378 MHz on the RP2350 M1; conservative 125 MHz on RP2040
# (stock boot2/QSPI timing, no vreg games).
set(CPU_SPEED "378" CACHE STRING "CPU speed in MHz")
if(PLATFORM STREQUAL "rp2040")
    set(CPU_VOLTAGE "VREG_VOLTAGE_1_15" CACHE INTERNAL "")
    set(FLASH_MAX_FREQ_MHZ "66" CACHE STRING "Flash max frequency in MHz")
else()
    set(CPU_VOLTAGE "VREG_VOLTAGE_1_60" CACHE INTERNAL "")
    set(FLASH_MAX_FREQ_MHZ "66" CACHE STRING "Flash max frequency in MHz")
endif()

# User-triggerable debug log toggle (default off)
option(DEBUG_LOGS_ENABLED "Enable verbose debug logging" OFF)
option(ENABLE_PS2_KEYBOARD "Enable PS/2 input" ON)

message(STATUS "BOOTMAN ${BOOTMAN_EXEC} - board=${PICO_BOARD} platform=${PLATFORM}, VIDEO=${VIDEO_TYPE}, CPU=${CPU_SPEED}MHz")

include(pico_sdk_import.cmake)

project(bootman C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

pico_sdk_init()

# Compile-time flags for the debug/PS2 toggles
if(DEBUG_LOGS_ENABLED)
    set(DEBUG_LOG_VALUE 1)
else()
    set(DEBUG_LOG_VALUE 0)
endif()
if(ENABLE_PS2_KEYBOARD)
    set(PS2_VALUE 1)
else()
    set(PS2_VALUE 0)
endif()

# Video defines per variant
if(VIDEO_TYPE STREQUAL "HDMI")
    set(BOOTMAN_VIDEO_DEFS VIDEO_HDMI VIDEO=HDMI)
else()
    set(BOOTMAN_VIDEO_DEFS VIDEO_VGA VIDEO=VGA)
endif()

# --------------------------------------------------------------------------
# Vendored driver copies from frank-c64 (COPIES, not external references).
# Layout mirrors c64/ so the relative includes stay untouched:
#   bootman/src/board_config.h        (trimmed M1 config)
#   bootman/drivers/<driver>/
# --------------------------------------------------------------------------
add_library(bootman_drivers STATIC
    drivers/${BOOTMAN_DRIVER_VIDEO}
    drivers/nespad/nespad.c
)
target_compile_definitions(bootman_drivers PRIVATE
    BOARD_M1
    CPU_CLOCK_MHZ=${CPU_SPEED}
    CPU_VOLTAGE=${CPU_VOLTAGE}
    FLASH_MAX_FREQ_MHZ=${FLASH_MAX_FREQ_MHZ}
    ENABLE_DEBUG_LOGS=${DEBUG_LOG_VALUE}
)
target_include_directories(bootman_drivers PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/drivers
)
target_link_libraries(bootman_drivers
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_clocks
)

add_library(ps2kbd STATIC
    drivers/ps2kbd/ps2kbd_mrmltr.cpp
    drivers/ps2kbd/ps2kbd_wrapper.cpp
)
target_compile_definitions(ps2kbd PRIVATE
    BOARD_M1
    KBD_CLOCK_PIN=0
)
pico_generate_pio_header(ps2kbd ${CMAKE_CURRENT_LIST_DIR}/drivers/ps2kbd/ps2kbd_mrmltr.pio)
pico_generate_pio_header(ps2kbd ${CMAKE_CURRENT_LIST_DIR}/drivers/ps2kbd/ps2kbd_mrmltr2.pio)
target_include_directories(ps2kbd PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/drivers/ps2kbd
)
target_include_directories(ps2kbd PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)
target_link_libraries(ps2kbd PRIVATE hardware_pio hardware_clocks)

add_library(fatfs INTERFACE)
target_sources(fatfs INTERFACE
    drivers/fatfs/ff.c
    drivers/fatfs/ffunicode.c
    drivers/fatfs/ffsystem.c
)
target_include_directories(fatfs INTERFACE drivers/fatfs)

add_library(sdcard INTERFACE)
pico_generate_pio_header(sdcard ${CMAKE_CURRENT_LIST_DIR}/drivers/sdcard/spi.pio)
target_sources(sdcard INTERFACE
    drivers/sdcard/sdcard.c
    drivers/sdcard/pio_spi.c
)
target_link_libraries(sdcard INTERFACE fatfs pico_stdlib hardware_clocks hardware_spi hardware_pio)
target_include_directories(sdcard INTERFACE ${CMAKE_CURRENT_LIST_DIR}/drivers/sdcard)

# --------------------------------------------------------------------------
# Boot manager executable
# --------------------------------------------------------------------------
add_executable(${BOOTMAN_EXEC}
    main.c
)

target_compile_definitions(${BOOTMAN_EXEC} PRIVATE
    BOARD_M1
    ${BOOTMAN_VIDEO_DEFS}
    CPU_CLOCK_MHZ=${CPU_SPEED}
    CPU_VOLTAGE=${CPU_VOLTAGE}
    FLASH_MAX_FREQ_MHZ=${FLASH_MAX_FREQ_MHZ}
    ENABLE_PS2_KEYBOARD=${PS2_VALUE}
    ENABLE_DEBUG_LOGS=${DEBUG_LOG_VALUE}
)
target_include_directories(${BOOTMAN_EXEC} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/drivers
)

pico_generate_pio_header(${BOOTMAN_EXEC} ${CMAKE_CURRENT_LIST_DIR}/drivers/sdcard/spi.pio)

set(BOOTMAN_LINK_LIBS
    pico_stdlib
    hardware_flash
    hardware_watchdog
    hardware_vreg
    hardware_dma
    hardware_irq
    hardware_pio
    hardware_clocks
    hardware_sync
    bootman_drivers
    ps2kbd
    sdcard
    fatfs
)
if(VIDEO_TYPE STREQUAL "HDMI")
    list(APPEND BOOTMAN_LINK_LIBS pico_multicore)
endif()

target_link_libraries(${BOOTMAN_EXEC}
    ${BOOTMAN_LINK_LIBS}
)

pico_enable_stdio_usb(${BOOTMAN_EXEC} 0)
pico_enable_stdio_uart(${BOOTMAN_EXEC} 0)

# Standard pico-sdk linker script: the image sits naturally in flash slot 0
# (0x10000000) with its own PICOBIN metadata block generated by the SDK.
pico_add_extra_outputs(${BOOTMAN_EXEC})

target_link_options(${BOOTMAN_EXEC} PRIVATE
    -Xlinker --print-memory-usage --data-sections -Wl,--gc-sections
)
