# SPDX-License-Identifier: MIT
# Copyright (c) 2026 fruit-bat
cmake_minimum_required(VERSION 3.12)

# Determine the MCU type and configure accordingly
# set(PICO_MCU "rp2040" CACHE STRING "Specify Pico MCU type")

# if ((${PICO_MCU} STREQUAL "rp2350"))
#     set(PICO_PLATFORM "rp2350-arm-s")
#     add_compile_definitions(PICO_MCU="RP2350")
# else()
#     set(PICO_PLATFORM "rp2040")
#     add_compile_definitions(PICO_MCU="RP2040")
# endif()

# Look for board file in local board directory
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/boards)
if (NOT PICO_BOARD)
    set(PICO_BOARD "pico")
endif()

# Pico SDC on Raspberry Pi 4
cmake_policy(VERSION 3.12)

# Pico SDC Toolchai on Windows 10
# cmake_policy(SET CMP0115 OLD)

# Pull in PICO SDK (must be before project)
include(pico_sdk_import.cmake)

# We also need PICO EXTRAS
include(pico_extras_import.cmake)

project(picodvi C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if (PICO_SDK_VERSION_STRING VERSION_LESS "2.0.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 2.0.0 (or later) required. Current version is ${PICO_SDK_VERSION_STRING}")
endif()

pico_sdk_init()

add_compile_definitions(PICO_MCU="${PICO_PLATFORM}")

# SuperSpec CF diag row ("CF: LBA...") in the menu: default OFF (user
# feedback 16 nov 2026 - remove the debug row from the shipped menu).
# Enable only for debugging: cmake -DZX_CF_DIAG=ON ... ; the cfDiag()
# machinery itself always stays compiled in for future use.
option(ZX_CF_DIAG "Show the SuperSpec CF diag line in the menu (debug)" OFF)
if(ZX_CF_DIAG)
  add_compile_definitions(ZX_CF_DIAG=1)
endif()

# CUB-Z HDMI diag row ("CZD: H..") in the menu: default OFF, entirely
# compiled out (struct, counters, row) unless the build passes
# -DZX_CZDIAG=ON.  DEBUG build only: evidence from the DVI render loop's
# cubz branch for the CUB-Z M1-P2 HDMI black-screen fault.
option(ZX_CZDIAG "Show the CUB-Z DVI diag line in the menu (debug)" OFF)
if(ZX_CZDIAG)
  add_compile_definitions(ZX_CZDIAG=1)
endif()

# CUB-Z DVI/HDMI display path: ON by default (M1-HDMI and every other DVI
# target keep the CUB-Z over HDMI).  The M2-AUTO target (the VGA+DVI dual
# pipeline, the tightest heap) overrides it to OFF in
# src/vgahdmi/CMakeLists.txt: the ~6.9 KB of cubz DVI statics collapsed
# that build's heap and the VGA stopped starting at all - on the AUTO the
# CUB-Z renders on the VGA scanvideo path only (proven since day one);
# with a CUB-Z selected over HDMI it shows no picture, by design.
option(ZX_CUBZ_DVI "Compile the CUB-Z DVI/HDMI display path" ON)
if(NOT ZX_CUBZ_DVI)
  add_compile_definitions(ZX_CUBZ_DVI=0)
endif()

add_compile_options(-Wall)

add_compile_definitions(HID_PROTOCOL_DEFAULT=HID_PROTOCOL_REPORT)
add_compile_definitions(PICO_FLASH_SPI_CLKDIV=6)

set(DVI_DEFAULT_SERIAL_CONFIG "pico_sock_cfg" CACHE STRING
	"Select a default pin configuration from common_dvi_pin_configs.h")

get_filename_component(PICO_FOLDER ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)

include_directories(
	${PICO_FOLDER}/PicoDVI/software/include
)

add_subdirectory(${PICO_FOLDER}/PicoDVI/software/libdvi "${CMAKE_CURRENT_BINARY_DIR}/libdvi")
add_subdirectory(${PICO_FOLDER}/PicoDVI/software/libsprite "${CMAKE_CURRENT_BINARY_DIR}/libsprite")
add_subdirectory(${PICO_FOLDER}/pico-dvi-menu "${CMAKE_CURRENT_BINARY_DIR}/pico-dvi-menu")
add_subdirectory(${PICO_FOLDER}/pico-emu-utils "${CMAKE_CURRENT_BINARY_DIR}/pico-emu-utils")
add_subdirectory(${PICO_FOLDER}/pico-hid-host "${CMAKE_CURRENT_BINARY_DIR}/pico-hid-host")
add_subdirectory(${PICO_FOLDER}/pico-nespad "${CMAKE_CURRENT_BINARY_DIR}/pico-nespad")

add_subdirectory(${PICO_FOLDER}/pimoroni-pico/drivers/fatfs "${CMAKE_CURRENT_BINARY_DIR}/pimoroni-pico/drivers/fatfs")
add_subdirectory(${PICO_FOLDER}/pimoroni-pico/drivers/sdcard "${CMAKE_CURRENT_BINARY_DIR}/pimoroni-pico/drivers/sdcard")
include_directories(${PICO_FOLDER}/pimoroni-pico/drivers/fatfs)
include_directories(${PICO_FOLDER}/pimoroni-pico/drivers/sdcard)

add_subdirectory(src  "${CMAKE_CURRENT_BINARY_DIR}/bin")

add_subdirectory(${PICO_FOLDER}/Zeta "${CMAKE_CURRENT_BINARY_DIR}/Zeta")

set(Z80_SHARED_LIBS                 NO  CACHE BOOL "")
set(Z80_WITH_Q                      YES CACHE BOOL "")
set(Z80_WITH_ZILOG_NMOS_LD_A_IR_BUG YES CACHE BOOL "")
# Z80's FindZeta reads the version from Z/version.h, so point it at the folder holding that
set(Zeta_INCLUDE_DIR                "${PICO_FOLDER}/Zeta/API")

add_subdirectory(${PICO_FOLDER}/Z80 "${CMAKE_CURRENT_BINARY_DIR}/Z80")
include_directories(${PICO_FOLDER}/Z80/API)
