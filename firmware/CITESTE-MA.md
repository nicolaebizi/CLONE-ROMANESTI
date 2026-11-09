# Firmware fruit-bat (pico-zxspectrum): CoBra 1, TIM-S Plus și ce s-a modificat

Data compilării: 9 noiembrie 2026 (versiunea 3: CUB-Z adăugat ca al 8-lea calculator).

## 1. Fișierele UF2 (VGA 640×480, sunet I2S)

| Fișier | Placă | Pico | TIM-S Plus |
|---|---|---|---|
| `MURMULATOR1_PICO1_VGA_I2S_640x480.uf2` | Murmulator 1 | Pico 1 (RP2040) | nu |
| `MURMULATOR1_PICO2_VGA_I2S_640x480.uf2` | Murmulator 1 | Pico 2 (RP2350) | **da** |
| `MURMULATOR2_PICO1_VGA_I2S_640x480.uf2` | Murmulator 2 | Pico 1 (RP2040) | nu |
| `MURMULATOR2_PICO2_VGA_I2S_640x480.uf2` | Murmulator 2 | Pico 2 (RP2350) | **da** |

**Variante cu display ILI9341 (SPI) și sunet I2S, fără VGA:**

| Fișier | Placă | Pico | TIM-S Plus |
|---|---|---|---|
| `MURMULATOR1_PICO1_ILI9341_I2S.uf2` | Murmulator 1 | Pico 1 (RP2040) | nu |
| `MURMULATOR1_PICO2_ILI9341_I2S.uf2` | Murmulator 1 | Pico 2 (RP2350) | **da** |

Conectare ILI9341 (ca în `schema_ILI9341_Murmulator1_Pico2.pdf`):

| ILI9341 | GPIO |
|---|---|
| CS | GP6 |
| RST | GP8 |
| LED / BL | GP9 |
| DC / RS | GP10 |
| SDA / MOSI | GP12 |
| SCK | GP13 |
| VCC | 3V3 |
| GND | GND |

Sunetul I2S este pe DATA = GP26 și BCLK = GP27. Tastatura este PS/2.

Pe Pico 1 TIM-S Plus nu e inclus: are nevoie de 192 KB RAM numai pentru memoria lui. Pe Pico 1 există toate celelalte calculatoare, inclusiv CoBra 1.

**Încărcare:**
1. Ține apăsat butonul BOOTSEL și conectează Pico-ul prin USB.
2. Copiază fișierul potrivit pe discul care apare: **RPI-RP2** pentru Pico 1, **RP2350** pentru Pico 2.

Compilările sunt verificate doar la compilare. CoBra 1 și TIM-S Plus au fost testate pe Mac, într-un harness care rulează codul real al emulatorului. Pe placă nu au fost încă testate.

## 2. Cardul SD

```
SD:/zxspectrum/
├── snapshots/
├── tapes/
└── disks/
    ├── cobra1/          imagini CoBra: .img (720K), .dsk (DSK/EDSK)
    ├── TIMS PLUS/       imagini TIM-S Plus (de ex. TIMSSYS.DSK). Numele are spațiu.
    └── CUBZ/            imagini CUB-Z: .dsk (EDSK) / .img (de ex. CUBZ_CPM.dsk)
```

Atenție: folderul este **`disks`**, la plural.

## 3. CoBra 1: ce am făcut

Am scris o implementare nouă, fidelă schemelor, fără hack-urile din proiectul vechi.

**Placa de bază v0.2.** Schema PDF are revizia 3.17e.
- PO preia R7 la fiecare refresh. LO6 preia O6 când PO trece din 1 în 0.
- Hărți de memorie pentru jumperii **80K + 80 chr/lin**, **80K** și **64K standard**.
- Memoria e paginată în blocuri de 8K.
- 8255 este decodat cu A0 XOR A5:
  - port A: tastatura K0–K5 și TI (intrarea de casetă)
  - port B: Kempston
  - port C: chenar, TO, difuzor, O5, O6, SO
- Întreruperea de cadru vine doar în modul Spectrum.
- Modulul de 80 de caractere pe linie funcționează pe VGA.

**ROM-uri:**
- Unified Boot de 16K
- SYSTEM ROM de 64K (OPUS / BASIC / DEVIL / CP/M 80 col). Bancurile se aleg cu SO = A14 și TO = A15.

**Interfața floppy v1.2:**
- Z80-CTC complet și 8272A în mod non-DMA.
- Conexiuni ca în schemă: INT→TRG0, ZC0→TRG1, ZC1→TRG2, ZC2→TC, NVS→TRG3.
- Imagini acceptate: IMG brut, DSK și EDSK, cu scriere pe card.

**DEVIL** rulează pe același hardware, fără niciun cod special. Imaginile Teledisk se convertesc cu `tools/td0_to_edsk.py`.

**Meniul F1 → C** conține discurile A:–D:, jumperii, ordinea sectoarelor pentru IMG DEVIL și RESET BASIC (K1).

**Testat pe Mac:**
- Unified Boot detectează corect 80K și 64K.
- CP/M din ROM: comanda `DIR` merge, iar TURTLE pornește.
- DEVIL: `CAT` merge, iar `LOAD "JETPAC"` încarcă jocul.

Detalii despre CoBra: `../uf2-cobra1/CITESTE-MA_CoBra1.md`.

## 4. TIM-S Plus: ce am reparat

**Simptom:** după alegerea discului, ecranul rămânea negru.

**Ce face ROM-ul original.** Am analizat dezasamblarea EPROM-ului de 2K. La pornire ROM-ul nu afișează nimic: șterge ecranul și așteaptă o tastă **1–5**. Cu tasta 1 încarcă sectorul de boot de pe discul din A: și îl rulează. Acest comportament este corect.

**Cauza.** Controlerul de disc (`src/ZxSpectrumHcFdc.cpp`) raporta în faza de rezultat registrul de stare MSR = `F0`, cu bitul EXM setat. Valoarea corectă pentru un 8272 real este `D0`. ROM-ul citește date cât timp vede EXM setat. Așa a citit octeții de rezultat ca și cum ar fi fost date, apoi a așteptat la nesfârșit un rezultat care nu mai venea.

**Reparația** este o singură linie: în faza de rezultat MSR este acum `D0`.

**A doua reparație: CP/M afișa ecran negru.** CP/M scria textul corect în memoria video separată (VRAM). În modul CP/M însă, emulatorul afișa pagina de sistem mapată la $4000, care era goală. Raster-ul TIM-S citește întotdeauna VRAM-ul, așa că acum ecranul vine mereu din VRAM (`screenPtr()` în `src/ZxSpectrum.h`). Modurile Spectrum nu se schimbă, ele foloseau deja VRAM.

**Rezultat cu `TIMSSYS.DSK`:** apare meniul „TIM-S PLUS - SYSTEM DISK”.

| Opțiune | Stare |
|---|---|
| 1 – Spectrum 48K / + | funcționează |
| 2 – Spectrum +2 | funcționează |
| 3 – Spectrum +3 / +3DOS | funcționează (drives A: și M:) |
| 4 – Spectrum 128K | funcționează |
| 5 – CP/M 2.2 | funcționează: „TIM-S PLUS CP/M 2.2”, `A>`, `DIR` listează discul |
| 6 – 48K BASIC + Interface 1 | funcționează |

**Precizări:**
- Sursa din care s-a portat TIM-S (`TIMSPLUS/FIRMWARE/fuse`, varianta „BMC2026”) este ea însăși o reconstrucție incompletă. Ea declară că modul CP/M și rasterul de 40/80 de coloane nu sunt terminate.
- `TIMSSYS.DSK` a fost generat cu același instrument; nu este dump-ul unei dischete reale.
- Pe schemă (pagina 12), pinul TC al lui 8272 este legat la RESET. Pe placa reală, deci, TC nu este activ în timpul transferurilor.

**Pornire:**
1. Reset → TIM-S Plus.
2. Montează `TIMSSYS.DSK` pe unitatea A.
3. Apasă **1**.
4. Alege din meniul de pe disc.

## 5. Meniul D (Disk images) curățat

- Rândurile **8** și **9** arată acum doar „TIMS PLUS drive A” / „TIMS PLUS drive B” și numele discului montat, fără contoarele de debug (RB, P, G, V, M, BR / C, R, ID, S, N, E, B).
- Rândurile de diagnostic **10. OP**, **11. RD** și **12. ST** au fost șterse.

## 6. CUB-Z (ICE Felix): al 8-lea calculator

Sursa portării: `proiecte_soft/TIMSPLUS/FIRMWARE/fuse` (machines/cubz.c, peripherals/disk/cubz_fdc.c, roms/cubz-1.4.rom), reconstruit după *Manual tehnic CUB-Z (1989)*.

**Ce este emulat:**
- Z80 la 2,5 MHz, fără întreruperi de cadru; la reset pornește de la F800.
- Memorie: 64K RAM plus 16K VRAM separată.
  - VRAM apare la 8000–BFFF când PIO-B bit 2 (+VDB) este 1.
  - EPROM-urile monitorului v1.4 sunt la E800/F000/F800 cât timp PIO-B bit 3 (/PROMOFF) este 1.
  - Sub EPROM-uri se poate scrie în RAM.
- Disc: i8272 la porturile 90/91, cu DMA 8257 pe canalul 2 (porturile 84/85/88), 4 unități. Imaginile acceptate sunt EDSK/DSK și IMG, iar scrierile ajung pe card.
- Ecran VDT-52S: 512×256 monocrom, verde pe negru, cu scroll hardware (portul F2).
- Tastatura VDT-52S este serială (ASCII pe PIO-B). Tastele PC sunt traduse în ASCII (layout US), inclusiv Ctrl+literă. Săgețile trimit ^E / ^X / ^S / ^D.

**Afișare pe VGA 640×480:** ecranul are 240 de linii, CUB-Z are 256. Un rând de text CP/M are 10 linii, dintre care 7 sunt linii de literă. Ca să încapă, se elimină cele 6 linii nefolosite de jos și ultima linie goală din primele 10 rânduri. Toate cele 25 de rânduri se văd, fără nicio linie de literă pierdută. La grafica pe tot ecranul lipsesc acele 16 linii.

**Meniuri:**
- Reset → **8. Reset CUB-Z**
- D → **10. CUB-Z drive A** și **11. CUB-Z drive B**. Se aleg cu săgețile și Enter. Imaginile sunt în `zxspectrum/disks/CUBZ`.
- În meniul D, deasupra „Disk images”, apare mesajul **„SE APASA TASTA 5 PENTRU REFRESH SD CARD”**. În lista de fișiere, tasta 5 reîncarcă conținutul cardului.

**Testat pe Mac** cu `CUBZ_CPM.dsk`:
- monitorul „cubZ V1.4” pornește;
- CP/M pornește automat: „I.C.E. - A.P.P. 57K CP/M ver 2.2/4”, apoi `A>`;
- `DIR` listează discul (GIL, TURBO, TINST, ORIZONT…);
- `TYPE` și scroll-ul funcționează.

**Limită:** pe variantele cu display ILI9341 (320×240), ecranul de 512 pixeli al CUB-Z nu este afișat. Pentru CUB-Z folosiți variantele VGA.

## 7. Ce rămâne de făcut

- TIM-S Plus: CP/M rulează deja în modul de 32 de coloane. Modurile video de 40/80 coloane descrise în Vol. III (cap. 10.5) nu sunt implementate.
- CoBra: STANDARD BOOT → D cu DISK02 revine în meniu, pentru că CHRIS.SYS de pe acel disc pare defect.
- Setările CoBra nu se salvează pe card.
- Nimic nu este salvat încă în git (branch `cobra1`). Recomand un commit.
