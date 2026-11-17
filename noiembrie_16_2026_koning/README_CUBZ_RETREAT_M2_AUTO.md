# M2-AUTO CUB-Z RETREAT — CUB-Z DOAR PE VGA (heap-ul nu ținea ambele pipeline-uri)

**File:**
- `ZX-MURMULATOR2_VGA_HDMI_AUTO_640x480x60Hz_SUPERSPEC_P2.uf2` — Murmulator 2 (Pico 2, RP2350),
  VGA/HDMI AUTO 640×480, I2S+DVI, boot-time auto-detect pe GPIO12-19
  SHA-256 (12): `0e82290e02a1` · full: `0e82290e02a195cd0d841f98638fd668ea1cffdffc340d922aec829a5338dc41`

**Base:** branch `main` (commit al retragerii, vezi GitHub) · pico2 / RP2350 · Release ·
`ZX_PLUS3_ROMS=ON` · `PICO_COPY_TO_RAM=0` · `ZX_CUBZ_DVI=OFF` (M2-AUTO) · build dir `_build_cubzretreat_20261117`

## De ce retragerea (SIMPTOM: VGA nu pornește de loc pe M2-AUTO)
Build-ul CUBZFAST al M2-AUTO nu pornea VGA-ul **deloc**: static-ele CUB-Z DVI
(+~6,9 KB BSS: LUT-ul dedicat 4 KB + paleta + fallback-ul) au prăbușit heap-ul
build-ului dual VGA+DVI (cel mai strâns heap al familiei).

| M2-AUTO build                          | BSS      |
|----------------------------------------|----------|
| fix CUB-Z #3 (65cdd39bc, starea veche) | 452,340  |
| CUBZFAST (39b853474, VGA MORT)         | 456,456  |
| **RETRAGEREA (acest build)**           | **449,520** |

- Retragerea scoate din compilare toată calea CUB-Z DVI (`zx_prepare_hdmi_scanline_cubz`
  + static-ele ei): BSS 456,456 → **449,520** (−6,936 exact static-ele cubz DVI) și text
  827,084 → 823,372 — sub build-ul fix-#3 cu 2,820 B, la nivelul dinaintea intrării
  CUB-Z pe DVI. VGA-ul revine la starea dovedită (scanvideo, testat din prima zi).
- **CUB-Z pe M2-AUTO = DOAR PE VGA, by design.** Dacă alegi CUB-Z cu monitor pe HDMI:
  nu are imaginație pe HDMI (calea generică); monitorul VGA este display-ul corect pentru CUB-Z.
- **M1-HDMI păstrează CUB-Z pe HDMI** — asset-ul release-ului rămâne build-ul fix-#3
  confirmat de utilizator (`e90bbb4b42140`, encoderul CUBZFAST a intrat doar în main după).
- Mecanism: flag per-target `ZX_CUBZ_DVI` (default ON; M2-AUTO îl forcează pe OFF în
  `src/vgahdmi/CMakeLists.txt`). Sanity-build M1-HDMI cu `ZX_CUBZ_DVI=ON`: PASSES,
  `zx_prepare_hdmi_scanline_cubz` prezent în map (12 referințe); pe M2-AUTO: 0 referințe.
- Calea CUB-Z VGA (`zx_prepare_scanvideo_scanline_cubz`, `ZxCubzLines.cpp`) NEATINSĂ —
  prezentă în map-ul M2-AUTO.

## Note
- Reflash obligatoriu (UF2 peste BOOTSEL). Înlocuiește build-ul CUBZFAST M2-AUTO
  retras din staging (`MURMULATOR2_PICO2_HDMI_AUTO_CUBZFAST_640x480.uf2`, VGA mort).
- Verificare hardware NU s-a făcut (n-a fost flashuit niciun device); BSS/map-ul
  sunt dovezi de build. VGA-ul „restaurat" așteaptă testul pe placă.
