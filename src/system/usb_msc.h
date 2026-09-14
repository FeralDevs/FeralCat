/**
 * @file usb_msc.h
 * @brief USB Mass Storage Class — exposes SD card as U-disk to a host PC.
 *
 * Enable:   SD_MMC.end() → sdmmc raw init → USBMSC.begin() → USB re-enumerate
 * Disable:  USBMSC.end() → sdmmc deinit → SD_MMC.begin() (FAT remount)
 * Active:   FAT-FS is unmounted; do NOT call SD_MMC file APIs while active.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Start USB MSC — dismount SD FAT-FS and expose raw sectors to host.
 *  Returns 1 on success, 0 on failure (FAT-FS is remounted on failure). */
int  usb_msc_enable(void);

/** Main-loop hook: close app-catalog handles before FAT is unmounted. */
void usb_msc_set_before_enable(void (*callback)(void*), void* context);

/** Stop USB MSC — detach from host and remount SD FAT-FS. */
void usb_msc_disable(void);

/** Returns 1 while MSC session is active (between enable and disable). */
int  usb_msc_is_active(void);

/** Cumulative bytes transferred (read + write) since last enable(). */
unsigned long usb_msc_bytes_transferred(void);

#ifdef __cplusplus
}
#endif
