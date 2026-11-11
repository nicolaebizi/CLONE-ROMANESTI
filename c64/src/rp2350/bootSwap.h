/*
 * bootSwap.h - "Boot Manager" flash-from-SD swap for FRANK C64 (RP2350).
 *
 * Duplicated on purpose from the ZX-firmware copy (pico-zxspectrum/src/
 * BootSwap.*): the two firmware builds share no build system, so each
 * carries its own adapted copy.
 *
 * Reads /sys/bootman_rp2350.bin from the SD card (the same FatFs instance
 * the disk loader uses), validates the PICOBIN header (marker 0xFFFFDED3 +
 * IMAGE_TYPE item 0x1021 = EXE|Secure|ARM|RP2350) BEFORE any flash erase,
 * erases + programs flash slot 0 (0x10000000) chunk-by-chunk (4 KiB) with
 * the SDK's RAM-resident flash routines, then watchdog-reboots straight
 * into the BOOTMAN v0.2 browser.
 *
 * Returns false (flash never touched!) when the file is missing/empty/
 * oversized, when the PICOBIN gate refuses a corrupt header or an RP2040
 * image, or on a mid-flash SD read failure. On success this function never
 * returns (the board reboots into the boot manager).
 */
#ifndef _BOOT_SWAP_H_
#define _BOOT_SWAP_H_

#define BOOT_SWAP_PATH_BOOTMAN  "/sys/bootman_rp2350.bin"
#define BOOT_SWAP_MAX_SIZE      (1024u * 1024u)

// Keep the one public entry point linkable from C++ (input_rp2350.cpp).
#ifdef __cplusplus
extern "C" {
#endif

int boot_swap_from_sd(void); // 0 = refused (message printed), no flash write

#ifdef __cplusplus
}
#endif

#endif // _BOOT_SWAP_H_
