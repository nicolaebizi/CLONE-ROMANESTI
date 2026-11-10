# CLONE ROMÂNEȘTI pe Murmulator (RP2040 / RP2350)

Emulator pentru calculatoarele românești compatibile ZX Spectrum și CP/M, construit pe proiectul
[fruit-bat/pico-zxspectrum](https://github.com/fruit-bat/pico-zxspectrum) (licență MIT) și extins
pentru plăcile Murmulator 1 și Murmulator 2.

## Calculatoare (meniul F1 → 6 Reset)

| # | Calculator | Ce este emulat |
|---|---|---|
| 1 | ZX Spectrum 48K | original fruit-bat |
| 2 | ZX Spectrum 128K | original fruit-bat |
| 3 | TURBO-SPECTRUM | porturi F5/CE/SM, ROM turbo |
| 4 | HC-2000 (ICE Felix) | i8272, CP/M, Interface 1 |
| 5 | CoBra 1 (Brașov) | placa de bază v0.2 rev 3.17e, interfață floppy v1.2 (Z80-CTC + 8272A), 80K / 80 caractere, Unified Boot + SYSTEM ROM 64K, CP/M, DEVIL     Meniu setare COBRA F1->C.|
| 6 | ZX Spectrum +3 | doar în build-urile cu ROM-uri +3 |
| 7 | TIM-S Plus | doar pe RP2350 ( datorita memoriei mici la pico1!): boot EPROM original, disc sistem, CP/M 2.2 |
| 8 | CUB-Z (ICE Felix) | monitor v1.4, 64K + 16K VRAM, i8272 cu DMA 8257, ecran 512×256, CP/M 2.2 |

## Firmware gata compilat (`firmware/`)

| Fișier | Placă | Pico | Video | Sunet |
|---|---|---|---|---|
| `MURMULATOR1_PICO1_VGA_I2S_640x480.uf2` | Murmulator 1 | Pico 1 (RP2040) | VGA 640×480 | I2S |
| `MURMULATOR1_PICO2_VGA_I2S_640x480.uf2` | Murmulator 1 | Pico 2 (RP2350) | VGA 640×480 | I2S |
| `MURMULATOR2_PICO1_VGA_I2S_640x480.uf2` | Murmulator 2 | Pico 1 (RP2040) | VGA 640×480 | I2S |
| `MURMULATOR2_PICO2_VGA_I2S_640x480.uf2` | Murmulator 2 | Pico 2 (RP2350) | VGA 640×480 | I2S |
| `MURMULATOR1_PICO1_ILI9341_I2S.uf2` | Murmulator 1 | Pico 1 (RP2040) | ILI9341 SPI | I2S |
| `MURMULATOR1_PICO2_ILI9341_I2S.uf2` | Murmulator 1 | Pico 2 (RP2350) | ILI9341 SPI | I2S |

Pentru încărcare: se ține apăsat **BOOTSEL**, se conectează Pico-ul prin USB și se copiază fișierul `.uf2`
pe discul care apare (RPI-RP2 sau RP2350).

Detalii despre fiecare calculator, taste, meniuri și limitări se găsesc în
[`firmware/CITESTE-MA.md`](firmware/CITESTE-MA.md).

## Cardul SD

```
SD:/zxspectrum/
├── snapshots/  tapes/
└── disks/
    ├── hc2000/
    ├── cobra1/       CoBra: .img 720K, .dsk (DSK/EDSK)
    ├── TIMS PLUS/    TIM-S Plus (numele are spațiu)
    └── CUBZ/         CUB-Z: .dsk (EDSK) / .img
    └── plus3/        +3
```

Meniul **D (HC2K,+3,TIMS PLUS,CUBZ)** montează dischetele. În listele de fișiere, tasta **5** reîncarcă
conținutul cardului.

## Structura repository-ului

| Director | Conținut |
|---|---|
| `pico-zxspectrum/` | sursa emulatorului (fruit-bat + modificările pentru clonele românești) |
| `firmware/` | UF2-urile finale, `CITESTE-MA.md`, discheta CP/M CUB-Z |
| `tools/` | `td0_to_edsk.py` (Teledisk → EDSK, păstrează ID-urile și întrețeserea), `bin2h.py` (ROM → header C) |
| `hosttest/` | harness care rulează nucleul emulatorului pe Mac/PC și salvează capturi de ecran |

## Compilare

Sunt necesare Pico SDK, `arm-none-eabi-gcc` și CMake. Bibliotecile fruit-bat se clonează alături de
`pico-zxspectrum`:

```sh
git clone https://github.com/fruit-bat/pico-hid-host
git clone https://github.com/fruit-bat/pico-nespad
git clone https://github.com/fruit-bat/pico-extras
git clone https://github.com/fruit-bat/PicoDVI
git clone https://github.com/pimoroni/pimoroni-pico
git clone https://github.com/fruit-bat/pico-dvi-menu
git clone https://github.com/fruit-bat/pico-emu-utils
git clone https://github.com/redcode/Z80.git
git clone https://github.com/redcode/Zeta.git
```

În `pimoroni-pico/drivers/fatfs/ffconf.h` trebuie setat `#define FF_USE_FIND 1`.

```sh
cd pico-zxspectrum
mkdir -p build/p2 && cd build/p2
cmake -DPICO_COPY_TO_RAM=0 -DPICO_PLATFORM=rp2350-arm-s -DPICO_BOARD=pico2 ../..   # Pico 2
# cmake -DPICO_COPY_TO_RAM=0 -DPICO_PLATFORM=rp2040 -DPICO_BOARD=pico ../..        # Pico 1
make -j8 ZX-MURMULATOR_VGA_I2S_AUDIO_640x480x60Hz ZX-MURMULATOR2_VGA_I2S_AUDIO_640x480x60Hz ZxSpectrumMurmulatorTftIli9341I2sPs2
```

## Surse și mulțumiri

- [fruit-bat/pico-zxspectrum](https://github.com/fruit-bat/pico-zxspectrum): emulatorul de bază (MIT)
- [redcode/Z80](https://github.com/redcode/Z80): nucleul Z80
- CoBra: scheme, ROM-uri și documentație de pe [cobrasov.com](http://www.cobrasov.com) (ElectroNNix)
- TIM-S Plus: *TIM-S Plus – șapte calculatoare într-unul singur* (1990, Pănescu, Dumitru) și dump-ul EPROM-ului original
- CUB-Z: *Manual tehnic CUB-Z* (1989, ICE Felix); reconstrucția Fuse „BMC2026” pentru TIM-S Plus și CUB-Z
- Bogdan Marcu @bmc2026 CUBZ SI TIMS PLUS
- ROM-urile și imaginile de dischetă incluse aparțin deținătorilor lor de drepturi și sunt incluse doar
pentru păstrarea istoriei acestor calculatoare.
