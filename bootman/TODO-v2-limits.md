# BOOTMAN — limite curente + plan v2

## v0.1 (curent) — limitări

1. **Doar RP2350 (pico2)**: `#if !PICO_RP2350 #error` în main.c. Toate
   driverele copiate din frank-c64 (c64/src/board_config.h → bootman/src)
   folosesc construcții RP2350-only:
   - `SYSINFO_PACKAGE_SEL` pentru eticheta chip (nu există pe RP2040);
   - `VREG_VOLTAGE_1_60` + `vreg_disable_voltage_limit()` + QMI timing
     pentru 378 MHz (pe RP2040 DIP: clock limitat, alt tabel de timpi XIP);
   - `PICOBIN` header-check specifică EXE|S|ARM|RP2350 (un binar RP2040 nu
     poate trece prin definiție; refuz intentional, nu bug).
2. **Video doar VGA** (optionala increment v0.2: driver HDMI din
   c64/drivers/HDMI.c pe PIO1 + core1, cu `graphics_get_buffer_line` la fel).
3. **Nu timeout auto**: doar confirmare explicită (cum cere spec).
4. **Max 64 fișiere .bin listate**, nume max 95 chars (LFN 255 suportat de
   ffconf, dar tabula UI e cuplată la 96).
5. **Slot 0 doar**: bootman nu suportă partiții/multi-slot (RP2350 flash
   partition tables) — v2 poate citi partition table din BOOT ROM.
6. PS/2 auto-repeat și NES repeat nu e implementat (edge-only).

## v2 — implementare RP2040

- board_config M1/RP2040 dedicat (SD 2..5, PS/2 0/1, NES 14..16 — semne
  ca pe M1, dar fără SYSINFO/QMI; clock aplicabil: 160–252 MHz);
- header-check: acceptă & REFUZ explicit binare RP2040 sau săge Plugins:
  v2 poate accepta imagini RP2040 doar dacă target-ul firmware-ului de
  boot-man e RP2040 (slotul 0 e RP2040 firmware); build la board `pico2`
  NU poate fl. RP2040 imagini — seamănă per session;
- NES/PS/2 driverele frank-c64 deja au branch RP2040 (nespad_begin cu
  cpu_khz → merge la fel), deci portul e minor la nivel de main.c.
