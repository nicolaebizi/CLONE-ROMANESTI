# CUBZFAST M1 + M2-AUTO — CUB-Z HDMI fast dedicated encoder (PERFORMANCE)

> **RETRAS (17 nov 2026):** `MURMULATOR2_PICO2_HDMI_AUTO_CUBZFAST_640x480.uf2` a fost
> scos din staging — static-ele CUB-Z DVI au prăbușit heap-ul build-ului dual M2-AUTO și
> VGA-ul nu mai pornea deloc. Înlocuit de build-ul de retragere
> `ZX-MURMULATOR2_VGA_HDMI_AUTO_640x480x60Hz_SUPERSPEC_P2.uf2` (CUB-Z DOAR PE VGA,
> BSS 449,520) — vezi `README_CUBZ_RETREAT_M2_AUTO.md`. M1 CUBZFAST rămâne valid.

**Files:**
- `MURMULATOR1_PICO2_HDMI_CUBZFAST_640x480.uf2` — Murmulator 1 (Pico 2, RP2350), HDMI 640×480×60, I2S
  SHA-256 (12): `c90bfa661461` · full: `c90bfa66146167bc4a65a7a147862d142ed0ffa2eeb8e8a11d6c3f4aad13f2fe`
- `MURMULATOR2_PICO2_HDMI_AUTO_CUBZFAST_640x480.uf2` — Murmulator 2 (Pico 2, RP2350), VGA/HDMI AUTO 640×480, I2S+DVI
  SHA-256 (12): `eadfe6c391f6` · full: `eadfe6c391f63492535f2cab265fb2be663e8e075cd8a0f5d3ba0e501a0725f8`

**Base:** 1ec8db089 (CZDIAG commit) · pico2 / RP2350 · Release · `ZX_PLUS3_ROMS=ON` · `PICO_COPY_TO_RAM=0` · build dir `_build_cubzfast_20261117`

## What changed (why the red flashes at DIR are fixed)
CUB-Z on HDMI rendered correctly but flashed red during CP/M disk operations
(DIR on CUBZ_CPM.dsk): the generic libdvi palette encode ran ~46 µs/line at
the 252 MHz scanline clock — right at the deadline — so SD/disk activity
stealing cycles made libdvi emit its solid-red late-scanline error lines.

Fix: dedicated per-line encoder for the CUB-Z 1bpp green/black palette
- 4 KB LUT (256 VRAM bytes → 4 TMDS words, green plane) derived once at
  init from the real palette symbol table (`tmds_setup_palette24_symbols`);
- blue/red planes = one constant word (both colours carry byte 0x00 on
  those channels), green margins = the same word;
- DC balance uses libdvi's own fixed-parity convention
  (`TMDS_FULLRES_NO_DC_BALANCE`: even pixels negative variant, odd pixels
  positive variant → aggregate per-line DC exactly balanced).  Word-level
  identity with the generic path is impossible (its variant choice follows
  a content-dependent accumulator), but the two variants decode to the
  same byte — the host test (`test/host/test_cubz_tmds.cpp`, 22/22 PASS,
  ASan/UBSan clean) cross-checks on 404 random-pattern lines (both
  timings): identical DECODED pixels on all 3 planes, every fast symbol a
  genuine palette variant, margins black.
- per-line cost ≈ 960 word stores ≈ ~10 µs at 252 MHz (~4× under the old
  encode; scanline period 31.8 µs → deadline met with ~3× margin).
- the generic path is kept as fallback; plane layout B/G/R, lines remap,
  scroll latch and VRAM source unchanged.
- BSS +4112 B (cubz statics ≈ 2.8 KB → 6.9 KB, the 4 KB LUT added).

## Notes
- Reflash required (UF2 over BOOTSEL).  The CZDIAG debug UF2 in this folder
  is untouched (it documents the CZDIAG commit).
- No hardware verification has been done by the build (no device was
  flashed); the red-flash fix is desk-proven, awaiting the board test.
