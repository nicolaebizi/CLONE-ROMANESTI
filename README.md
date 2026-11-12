# CLONE ROMÂNEȘTI pe Murmulator (RP2040 / RP2350)

Emulator pentru calculatoarele românești compatibile ZX Spectrum și CP/M, construit pe proiectul
[fruit-bat/pico-zxspectrum](https://github.com/fruit-bat/pico-zxspectrum) (licență MIT) și extins
pentru plăcile Murmulator 1 și Murmulator 2.

## Calculatoare (meniul F1 → 6 Reset)

| # | Calculator | Ce este emulat |
|---|---|---|
| 1 | ZX Spectrum 48K | original fruit-bat |
| 2 | ZX Spectrum 128K | original fruit-bat |
| 3 | TURBO-SPECTRUM | porturi F5/CE/SM, ROM turbo  WWW.COBRASOV.ORG |
| 4 | HC-2000 (ICE Felix) | i8272, CP/M, Interface 1 |
| 5 | CoBra 1 (Brașov) | placa de bază v0.2 rev 3.17e, interfață floppy v1.2 (Z80-CTC + 8272A), 80K / 80 caractere, Unified Boot + SYSTEM ROM 64K, CP/M, DEVIL     Meniu setare COBRA F1->C. WWW.COBRASOV.COM|
| 6 | ZX Spectrum +3 | doar în build-urile cu ROM-uri +3 |
| 7 | TIM-S Plus | doar pe RP2350 ( datorita memoriei mici la pico1!): boot EPROM original, disc sistem, CP/M 2.2 |
| 8 | CUB-Z (ICE Felix) | monitor v1.4, 64K + 16K VRAM, i8272 cu DMA 8257, ecran 512×256, CP/M 2.2 |
| 9 | HC88 (ICE Felix) | pe RP2040 (Pico1) și RP2350 (Pico2): clonă ZX românească, boot de pe HC88SYS.DSK/EDSK, tasta de reset 9 |

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
## RELEASE 1 V 0.2
Are in plus un meniu de incarcare ROM 16k in calculatorul 48k .Imaginile se pun in sd:/zxscpectrum/roms
La repornirea placii calculatorul revine la basic original !

## Cardul SD   !!!NU UITATI DE TASTA 5 REFRESH DUPA CE ATI PUS CU PC ALTE FISIERE PE CARD - IN FIECARE MENIU 
  

```
SD:/zxspectrum/
├── snapshots/  tapes/
└── disks/
    ├── hc2000/
    ├── cobra1/       CoBra: .img 720K, .dsk (DSK/EDSK)
    ├── TIMS PLUS/    TIM-S Plus (numele are spațiu)
    └── CUBZ/         CUB-Z: .dsk (EDSK) / .img
    └── plus3/        +3
    └── HC88/         HC88: .dsk (DSK/EDSK)
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

## Compilare din surse (Pico 2 / RP2350)

Instrucțiunile de mai jos pornesc de la o clonare curată a acestui repository și construiesc
firmware-ul VGA 640×480 cu sunet I2S pentru Murmulator 1 și Murmulator 2.

### 1. Unelte necesare

Pe Ubuntu/Debian:

```sh
sudo apt update
sudo apt install -y git cmake ninja-build build-essential gcc-arm-none-eabi libnewlib-arm-none-eabi
```

Este necesar și Python 3 (folosit de unele unelte Pico SDK).

### 2. Descarcă dependențele

Rulează comenzile din directorul rădăcină al repository-ului, lângă directoarele
`pico-zxspectrum/` și `firmware/`:

```sh
git clone https://github.com/fruit-bat/pico-hid-host.git
git clone https://github.com/fruit-bat/pico-nespad.git
git clone https://github.com/raspberrypi/pico-extras.git
git clone --branch audio https://github.com/fruit-bat/PicoDVI.git
git clone https://github.com/pimoroni/pimoroni-pico.git
git clone https://github.com/fruit-bat/pico-dvi-menu.git
git clone https://github.com/fruit-bat/pico-emu-utils.git
git clone https://github.com/redcode/Z80.git
git clone https://github.com/redcode/Zeta.git
```

PicoDVI trebuie clonat de pe ramura `audio`; aceasta furnizează interfața audio HDMI
așteptată de sursele proiectului. Pico SDK este descărcat automat de CMake, la versiunea
**2.1.1**, pentru a evita incompatibilitatea assembly din PicoDVI cu SDK 2.2.x.

Activează căutarea directoarelor în FatFs (necesară pentru listarea fișierelor de pe SD):

```sh
sed -i 's/^#define FF_USE_FIND[[:space:]]\+0/#define FF_USE_FIND 1/' pimoroni-pico/drivers/fatfs/ffconf.h
```

### 3. Configurează și compilează

Din directorul rădăcină al repository-ului:

```sh
export PICO_SDK_FETCH_FROM_GIT=1
export PICO_EXTRAS_PATH="$PWD/pico-extras"

cmake -S pico-zxspectrum -B pico-zxspectrum/build-pico2 -G Ninja \
  -DPICO_COPY_TO_RAM=0 \
  -DPICO_PLATFORM=rp2350-arm-s \
  -DPICO_BOARD=pico2 \
  -DPICO_SDK_FETCH_FROM_GIT_TAG=2.1.1

cmake --build pico-zxspectrum/build-pico2 --target ZX-MURMULATOR_VGA_I2S_AUDIO_640x480x60Hz --parallel 2
cmake --build pico-zxspectrum/build-pico2 --target ZX-MURMULATOR2_VGA_I2S_AUDIO_640x480x60Hz --parallel 2
```

Fișierele `.uf2` rezultate se găsesc în subdirectoarele de build ale țintelor CMake.
Pentru numele exacte ale fișierelor, caută `*.uf2` în `pico-zxspectrum/build-pico2/`.

### 4. Build automat

La fiecare push pe ramura `main`, GitHub Actions compilează automat cele două ținte VGA
I2S pentru Pico 2. Configurația CI folosește aceleași versiuni de Pico SDK și PicoDVI
ca instrucțiunile de mai sus.

Pentru alte sisteme de operare, instalează echivalentele CMake, Ninja, Git și
Arm GNU Toolchain înainte de a urma pașii 2–3.

## Surse și mulțumiri

- [fruit-bat/pico-zxspectrum](https://github.com/fruit-bat/pico-zxspectrum): emulatorul de bază (MIT)
- [redcode/Z80](https://github.com/redcode/Z80): nucleul Z80
- Sorin CISMAS WWW.COBRASOV.ORG -> TURBO SPECTRUM
- CoBra: scheme, ROM-uri și documentație de pe [cobrasov.com](http://www.cobrasov.com) (ElectroNNix)
- TIM-S Plus: *TIM-S Plus – șapte calculatoare într-unul singur* (1990, Pănescu, Dumitru) și dump-ul EPROM-ului original
- CUB-Z: *Manual tehnic CUB-Z* (1989, ICE Felix); reconstrucția Fuse „BMC2026” pentru TIM-S Plus și CUB-Z
- Bogdan Marcu @bmc2026 CUBZ, TIMS PLUS SI HC-88 -> DISK TIMS PLUS, HC-88 : bmc.net2000@gmail.com   IL PUTETI CONTACTA PENTRU ACESTEA !!!  MOMENTAN SUNT IN LUCRU SI NU SUNT FINALE!!!
- ROM-urile și imaginile de dischetă incluse aparțin deținătorilor lor de drepturi și sunt incluse doar
pentru păstrarea istoriei acestor calculatoare.
- Si nu in ultimul rand CLAUDE, SI OPENCODE.- GLM-5.3-Flash Free CU PINGUINII MEI 4 LA NUMAR !!!
