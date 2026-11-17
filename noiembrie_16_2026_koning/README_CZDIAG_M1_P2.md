# CZDIAG M1-P2 — CUB-Z HDMI diagnostic build (DEBUG, gated)

**File:** `ZX-MURMULATOR_HDMI_I2S_AUDIO_640x480x60Hz_CZDIAG_M1_P2.uf2`
**SHA-256 (12):** `c2bba673ed83`
**Full SHA-256:** `c2bba673ed83a697c65ded5425124a2c06d477bae3ca84863403263d63459d25`
**Base:** 65cdd39bc (after 514fc0df DOC) · MURMULATOR M1 (pico2, RP2350) · 640x480x60Hz HDMI · I2S audio · Release · ZX_PLUS3_ROMS=ON · PICO_COPY_TO_RAM=0 · `-DZX_CZDIAG=ON` · build dir `_build_czdiag_20261116`

## Purpose
Real evidence from the board for the CUB-Z black-screen-on-M1-P2-HDMI fault
(after 3 desk-proven fixes that all failed). Flash this UF2, load a CUB-Z
snapshot/disk, open the menu (the row repaints live).

## The diag row (menu row 1, CUB-Z machine only, `#ifdef ZX_CZDIAG`)

    CZD: H<calls/sec> <h_active> S<setup> C<cubz-pred> O<ob><og>

| Field  | Meaning |
|--------|---------|
| `H`    | cubz-branch calls in the LAST second (latched on every 60th frame; 240 expected when the render loop draws CUB-Z lines; 0 in menu mode — the branch is skipped there) |
| next   | `dvi0.timing->h_active_pixels` as the code sees it (640 expected for this timing) |
| `S`    | 1 = `tmds_setup_palette24_symbols` ran, 0 = never called |
| `C`    | `zxSpectrum.cubz()` as the render loop sees it, sampled every line, branch taken or not (1 = CUB-Z mode active) |
| `O`    | pixbuf plane parity, two digits: `ob` = B-plane write base (tmdsbuf) %4, `og` = G-plane offset in words (h_active/2) %4 (both 0 expected for 640-wide timing) |

## Expected healthy reading (CUB-Z mode, game running, menu closed)
`CZD: H240 640 S1 C1 O00`

## Notes
- Instrumentation is fully compiled out without `-DZX_CZDIAG=ON` (verified:
  default-off build of the 4 touched TUs has zero `zxCz` symbols); BSS delta
  in this build = 20 bytes (12-byte snapshot + 2 x 4-byte counters).
- Diagnostic build only — NOT a fix, NOT a release. The CUBZFIX UF2s are
  untouched. No hardware verification has been done (no device was flashed).
