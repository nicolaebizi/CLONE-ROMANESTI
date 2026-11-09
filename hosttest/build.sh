#!/bin/sh
F=../pico-zxspectrum/src
clang++ -std=c++17 -O2 -w -DZ80_STATIC -DPICO_RP2350=1 $EXTRA -Istub -I$F -I../pico-emu-utils/src -I../Z80/API -I../Zeta/API -o cobra_host cobra_host.cpp $F/ZxSpectrum.cpp $F/CobraFloppy.cpp $F/ZxSpectrumHcFdc.cpp $F/ZxSpectrumAy.cpp $F/PulseProc*.cpp ../pico-emu-utils/src/InputStream.cpp ../pico-emu-utils/src/OutputStream.cpp ../pico-emu-utils/src/SizingOutputStream.cpp $F/ZxSpectrumJoystick.cpp $F/ZxSpectrumMouse.cpp z80.o
