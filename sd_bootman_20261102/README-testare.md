# BOOTMAN v0.3.1 — testare SD card (Murmulator RP2350, M1)

`sd_bootman_20261102/` este un șablon de card SD pentru BOOTMAN.
Copiați conținutul folderului pe rădăcina cardului FAT/exFAT:

```
/pico2/c64_rp2350.bin     <- firmware Commodore 64 (frank-c64, RP2350, M1, VGA)
/pico2/bootman_rp2350.bin <- BOOTMAN v0.3.1 VGA (auto-update bootman -> bootman)
/pico2/*.bin              <- (alt firmware RP2350, opțional)
/pico2/*.uf2              <- (opțional: versiuni UF2 ale acelorași imagini)
/pico1/*.uf2              <- firmware RP2040 (DOAR .uf2, family 0xE48BFF56);
                             demo inclus: ZxSpectrum4PinAudioVga1111Ps2_
                             720x576x50Hz.uf2 (594 KiB, sub bugetul de 1 MiB)
/sys/bootman_rp2350.bin   <- BOOTMAN v0.3.1 VGA (folosit de opțiunea de meniu
                             "Boot Manager" din firmware-ul ZX RP2350 și C64;
                             identică cu /pico2/bootman_rp2350.bin)
```

Structura pe chip: `/pico1` = imagini RP2040 (`.uf2`, family `0xE48BFF56`),
`/pico2` = imagini RP2350 (`.bin` PICOBIN sau `.uf2`, family `0xE48BFF59`).
Managerul RP2350 nu vede `/pico1`, iar managerul RP2040 nu vede `/pico2`.

## Ce este BOOTMAN

Firmware mic (*.uf2 din `uf2_bootman_20261102/`), flash-uit o singură dată la
adresa 0x10000000 (slot 0). Rulează la FIECARE pornire; nu e dependent de
firmware-ul ZX/C64 — este o aplicație standalone.

## Primul boot (instalare BOOTMAN)

1. Placa repornită cu BOOTSEL apăsat → drive `RPI-RP2` pe USB.
2. Copiați `bootman-murmulator-RP2350-VGA-v0.3.1.uf2` (226 KB) în `RPI-RP2`.
   **VGA v0.3.1 = versiunea recomandată pentru RP2350 M1;** v0.3/v0.2/v0.1 VGA din
   `uf2_bootman_20261102/` rămân ca fallback.
   *v0.3.1 fix: SD rewind bug (PRE-FLIGHT→negru) — flash_uf2_stream() nu făcea
   f_lseek înapoi la 0 după scan, prima f_read întorcea 0 → FLASH_SD_READ →
   ecran negru permanent, flash-ul nu rula niciodată.*
3. Placa reboot-ază în BOOTMAN: pe VGA apare header-ul
   `BOOTMAN v0.3.1 Murmulator M1 RP2350-…` plus lista fișierelor din `/pico2`.

## UF2 sau BIN?

Ambele formate sunt acceptate în `/pico2` — lista arată tag-ul `BIN` sau
`UF2` lângă dimensiune:

- `.bin` = imaginea PICOBIN brută (verificare header `EXE|Secure|ARM|RP2350`,
  marker 0xFFFFDED3 + IMAGE_TYPE 0x1021);
- `.uf2` = format fork al repo-ului (bloc 512 B: familyID la offset 28,
  addr la 12, payload 256 B la 32, magic-end 0x0AB16F30 — verificat cu xxd
  pe toate fișierele .uf2 ale repo-ului). BOOTMAN citeste bloc cu bloc,
  fără buffer complet în RAM:
  - blocurile payload trebuie să înceapă la `0x10000000` și să fie
    secvențiale (pas 256 B);
  - blocurile „image table" (addr `0x10FFFF00`, flags `0xA000`,
    family `0xE48BFF57`) sunt ignorate (nu poartă date flash);
  - doar family RP2350 `0xE48BFF59` e acceptat; family RP2040
    `0xE48BFF56` sau orice alt family → REFUZ („img RP2040 will not
    be used");
  - dimensiunea imaginii (ultimul payload − 0x10000000 + 256) trebuie să
    rămână ≤ 1 MiB — fișierul .uf2 poate fi ~2.3× mai mare decât imaginea;
  - la flash, fișierul .uf2 este RE-CITIT din SD și scris chunk-cu-chunk
    (erase 4 KiB → payload → program), identic cu fluxul .bin.
- Un .uf2 acceptat în `/pico2` va fi flash-at direct, ca un .bin.

## Varianta RP2040 (BOOTMAN v0.2-RP2040, placa pico)

- browser `/pico1`, acceptă DOAR `.uf2` cu family `0xE48BFF56`; fișierele
  `.bin` NU sunt listate și sunt refuze la flash („RP2040: flash using
  .uf2 only") — un .bin RP2040 începe cu boot2 (fără header PICOBIN
  verificabil, deci nu există magic safe de validat);
- `.uf2` cu family RP2350 `0xE48BFF59` sau alt family → REFUZ (bootman
  rămâne viu, mesaj „img RP2350 will not be used");
- buget imagine identic: ≤ 1 MiB derivat din payload (demo-ul din
  `/pico1` are 594 KiB — se încadrează);
- flash la `0x10000000`, chunk-cu-chunk, identic cu varianta RP2350;
  funcțiile erase/program sunt legate în SRAM (verificat cu `nm`:
  `bootman_erase_program_chunk` la `0x200000c0`,
  `flash_range_erase/program` la `0x20000234/0x200002c0`);
- `flash_range_erase/program` există identic în SDK pe RP2040 — același
  mecanism swap-în-RAM ca pe RP2350;
- se instalează cu `bootman-murmulator-v0.2-RP2040.uf2` în drive-ul
  `RPI-RP2` (BOOTSEL).

## Varianta HDMI (BOOTMAN v0.2-HDMI, RP2350 M1)

- driver HDMI din frank-c64 (vendored `bootman/drivers/HDMI.c|h`,
  PIO1 + 4 canale DMA, TMDS prin 3 perechi diferențiale pe M1);
- video task pe core1: DMA IRQ + pornirea scanout-ului rulează pe core1
  (`graphics_set_defer_irq_to_core1(true)` + `multicore_launch_core1`);
  la flash, core1 este oprit (`multicore_reset_core1`) înainte de
  `graphics_shutdown()`/abort DMA — același flux XIP-safe ca VGA;
- browser `/pico2` + validări identice cu v0.2 VGA (PICOBIN 0x1021 /
  UF2 family `0xE48BFF59`, ≤ 1 MiB);
- se instalează cu `bootman-murmulator-v0.2-HDMI.uf2`.

## Alegeri firmware

- lista arată pentru fiecare fișier dimensiunea în KiB + tag-ul `BIN`/`UF2`;
- navigare: sageți sus/jos (PS/2) sau D-pad (NES pad);
- confirmare: `ENTER` (PS/2) sau `A` (NES pad);
- ecran de confirmare explicită (fără timeout) — header `FLASH FIRMWARE?
  (BIN/UF2)` — arată nume, dimensiune, format, slotul 0x10000000 și
  avertismentul că boot manager-ul se șterge pe sine;
- ENTER/A pe ecranul de confirmare → BOOTMAN validează:
  - dimensiune imagini > 0 și ≤ 1 MiB (.bin: dimensiunea fișierului;
    .uf2: dimensiunea derivată din payload);
  - .bin: header PICOBIN valid: `EXE | Secure | ARM | RP2350`
    (marker 0xFFFFDED3 + IMAGE_TYPE 0x1021). Binar RP2040 sau corupt →
    REFUZ, bootman rămâne viu;
  - .uf2: scanarea streaming descrisă la „UF2 sau BIN?" (family RP2350
    secvențial; RP2040/alt family → REFUZ, bootman rămâne viu);
  - apoi erază și programează chunk-cu-chunk (4 KiB) la 0x10000000 și face
    reset (watchdog_reboot) → pornește firmware-ul ales.

## Boot Manager din meniul firmware-urilor (ZX RP2350 / C64) — 20261103

Pas suplimentar de testare, din firmware în firmware (fără BOOTSEL):

**De la ZX: meniu → `-> Boot Manager` (autocurăță)** — firmware-ul ZX RP2350
oprește singur nucleul de randare (core1), abortează toate canalele DMA
(audio mut) și flash-uiește `/sys/bootman_rp2350.bin` în slot 0
(`0x10000000`), chunk-cu-chunk (4 KiB, buffer staging 4 KiB în SRAM), apoi
face watchdog_reboot. Refuz curat (fără atingerea flash-ului) dacă:
imaginea lipsește, e > 1 MiB, sau header-ul PICOBIN nu e
`marker 0xFFFFDED3 + IMAGE_TYPE 0x1021` (EXE|Secure|ARM|RP2350).

1. Flash-uit firmware-ul ZX RP2350 (`uf2_plus3_models_rp2350_20261101/`) SAU
   firmware-ul C64 (`uf2_c64_murmulator_20261101/`) pe RP2350;
2. Card SD cu template-ul din `sd_bootman_20261102/` (v. layout de sus, nu
   uita `/sys/bootman_rp2350.bin`);
3. Din ZX: meniu → `-> Boot Manager` → confirmare → se flash-uiește
   `/sys/bootman_rp2350.bin` în slot 0 + reset;
4. Din C64: F10 deschide Disk UI → alege *orice* imagine → în fereastra
   `Action` alege `-> Boot Manager` → ENTER (armare) → din nou ENTER
   (FLASH+RESET) → se flash-uiește    `/sys/bootman_rp2350.bin` + reset;
5. Așteptat: după reset pornește browserul BOOTMAN v0.3 (aceeași validare
   PICOBIN: marker 0xFFFFDED3 + IMAGE_TYPE 0x1021; imagine RP2040/coruptă →
   refuz, firmware-ul curent rămâne viu).

### De la C64: → Boot Manager

Implementat în `c64/src/rp2350/bootSwap.c|h` (copie C adaptată a lui
BootSwap din firmware ZX; aceleași constante PICOBIN + același flux XIP-safe
ca `Cartridge.cpp flash_block()`):

1. `F10` deschide Disk UI (listă din `/pico2`, `/`);
2. selectează *orice* fișier → ferestra `Action` oferă trei opțiuni:
   `Load (Run)` / `Mount (Insert)` / `-> Boot Manager`;
3. `-> Boot Manager` + ENTER = **armare** (subliniat); ENTER a **doua** oară =
   flash + reset; `ESC` dezarmează;
4. la execuție: oprește video core1 (`multicore_lockout`), mută DMA/I2S,
   validează `/sys/bootman_rp2350.bin` (marker `0xFFFFDED3` + IMAGE_TYPE
   `0x1021`, max 1 MiB) — refuz curat, flashul NU e atins la orice eroare;
5. flash chunk-cu-chunk (4 KiB staging în SRAM, funcția erase/program e
   legată în SRAM — verificat cu `nm`: simbol la `0x20000180`), apoi
   `watchdog_reboot` → pornește BOOTMAN v0.3;
6. eșec la citirea SD *pe parcursul* flash-ului → HOLD pe mesajul
   „BOOTSEL to recover" (fără auto-reboot), recuperare BOOTSEL la fel ca
   pentru BOOTMAN.

## Fallback BOOTSEL (recuperare)

BOOTSEL este în BOOT ROM, NU depinde de slotul 0:

- dacă flash write eșuează pe parcurs (SD scoasă, etc.), bootman rămâne blocat
  cu ecran roșu "SD read FAILED mid-flash" (sau "UF2 stream changed
  mid-flash" pentru .uf2) și NU reboot-ază singur;
- în orice stare stricată: țineți BOOTSEL apăsat la alimentare/plug USB,
  apare `RPI-RP2` și refăcuți flash-ul cu orice .uf2 (inclusiv BOOTMAN).

## Limita

Slotul 0 are 4 MiB pe placa M1; BOOTMAN acceptă imagini de maximum 1 MiB
(bujet ales conservator; firmware-le ZX/C64 de aici au ~0.4 MiB).

Testat doar prin analiză de format (PICOBIN + UF2-offset-uri xxd + parser
self-test pe fișiere reale), NU pe hardware.
