# DOCUMENTAȚIE v0.11-superspec

**RELEASE:** v0.11-superspec @ `67e84e29a` (incl. fix CUB-Z pe HDMI)
**PLATĂ:** Murmulator 1 / Murmulator 2, RP2350 (Pico 2), drag&drop UF2 prin BOOTSEL
**DATA:** 16 noiembrie 2026

---

## CE ESTE v0.11

Firmware-ul pico-zxspectrum rulează pe Pico 2 un selector de mașini: **11 mașini ZX + clone românești** (HC2K, TIMS PLUS, CUB-Z, HC88, HC 3/„+3s" etc.) pe plăcile Murmulator M1 și M2, cu ieșiri VGA / HDMI / TFT ILI9341 și audio I2S.

Noutatea mare a v0.11: **SuperSpec #11** (al 11-lea calcul din seria lui Roelof Koning) — un ZX48 cu un DOS modern pe ROM, cu disc CF emulat pe cardul SD.

## SUPER SPEC — CALCULUL #11 (cum îl pornești și cum îl folosești)

**Intrarea în SuperSpec:** tasta **E** în meniul de mașini (sau tasta `0` la reset) → apare meniul autentic de coldstart al ROM-ului (bank 224 = superDOS).

**CF-ul emulat (imaginea de disc):**
- Firmware-ul creează **automat o imagine de 3.67 GB** (`zxspectrum/cf/SUPERSPEC.CF` pe cardul SD).
- **Prima accesare a CF-ului = CREAREA imaginii: 1–3 minute în care pare înghețat = NORMAL.** Nu atinge nimic, nu faci reset — așteaptă până termină (o singură dată, la prima utilizare).
- Cardul SD trebuie să aibă **~4 GB liberi**, altfel creația nu merge.

**DOS-ul lui Koenig (superDOS, din ROM):**

| Comandă | Ce face |
|---|---|
| `FORMAT 1;"nume"` | formatează/ creează fișierul — **instant!** |
| `SAVE *"m";1;"nume"` | salvează programul pe CF |
| `LOAD *"m";1;"nume"` | încarcă programul de pe CF |
| `CAT` | **filemanager** interactiv (Q = exit, H = help) |
| `CAT 1` | listare simplă a fișierelor |

**Schimbarea imaginii CF:** din meniul firmware-ului: **D → CF-SUPERSPECS → S: CF IMAGINE**. Alegi orice `*.CF` din `zxspectrum/cf` de pe cardul SD; mount-ul e instant, apoi faci reset (meniul Reset → E) ca să pornești DOS-ul pe imaginea nouă.

## CELE 6 VARIANTE

| # | Placă | Afișaj | Fișier (sufix) | Stare test |
|---|---|---|---|---|
| 1 | Murmulator 1 (M1) | VGA 640x480x60 + I2S | `_M1_P2` | **TESTAT pe hardware** |
| 2 | Murmulator 2 (M2) | VGA 640x480x60 + I2S | `_P2` (M2 VGA) | **TESTAT pe hardware** |
| 3 | Murmulator 2 (M2) | AUTO VGA/HDMI (detectare la boot) | `_P2` (AUTO) | TESTAT pe VGA; **calea HDMI = build CUB-Z reparat, validare hardware în curs** |
| 4 | Murmulator 1 (M1) | TFT ILI9341 320x240 + I2S + PS/2 | `ZxSpectrumMurmulatorTftIli9341I2sPs2_..._M1_P2` | **TESTAT pe hardware** (16 nov 2026) |
| 5 | Murmulator 2 (M2) | TFT ILI9341 320x240 + I2S + PS/2 | `ZxSpectrumMurmulator2TftIli9341I2sPs2_..._M2_P2` | build-verified; testare pe hardware în curs |
| 6 | Murmulator 1 (M1) | HDMI FIX 640x480x60 (pimoroni) + I2S | `_M1_P2` (HDMI) | build CUB-Z reparat, build-verified; **validare hardware în curs** |

Notă: NU există variantă M1-AUTO (detectarea VGA/HDMI la boot e doar pe MURMULATOR2, wiring GPIO12-19).

## DEPANARE

- **Ecran negru la prima accesare CF = AȘTEAPTĂ.** Nu e blocat — e creația imaginii de 3.67 GB (1–3 minute, doar prima dată). Dacă faci reset în mijlocul creației, relucrează de la început.
- **Cardul nu are ~4 GB liberi = CF-ul nu merge.** Eliberează spațiu sau schimbă cardul.
- **Debug CF:** rândul „CF: LBA..." nu mai apare în meniu (default OFF). Pentru depanare, recompilează cu `ZX_CF_DIAG=ON`.
- **CUB-Z pe HDMI (M1-HDMI și M2-AUTO): REPARAT în v0.11** — pe versiunile vechi imaginea era garbage/blank. **Reflash obligatoriu** cu build-urile din acest release (SHA-256: `918ae6666cfe...` M1-HDMI, `4c55d742afa6...` M2-AUTO).
- După schimbarea imaginii CF, comutarea se face cu RESET (meniul Reset → E).

## CREDITE

- **ROELOF KONING** — ROM-ul ZX48 SuperSpec (supr190712, bank 224 = superDOS) + documentația structurii FS de pe CF.
- **CRISTIAN SECARĂ** — ROM-urile +3s.
- **Utilizatorul** — proiectul, integrarea (Murmulator M1/M2, meniul D → CF-SUPERSPECS → S: CF IMAGINE) + validarea pe hardware.
- **fruit-bat** — baza: pico-zxspectrum.

---

*Asset-uri release: 6 UF2 (vezi README-ul fiecărei variante în `CLONE_ROMANESTI/noiembrie_16_2026_koning/`) + acest document.*
