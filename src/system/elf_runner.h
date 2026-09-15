/**
 * @file elf_runner.h
 * @brief Load and run a native ELF app from the SD card (spike).
 *
 * Wraps Espressif's elf_loader (lib/elf_loader): reads an .elf into RAM,
 * relocates it, calls its app_main, then frees everything. Internal-RAM exec
 * for now (small apps); PSRAM comes later. NOT sandboxed — see the wiki.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Run the ELF at `path` (SD_MMC path, e.g. "/apps/hello/app.elf").
 * On success returns the app's return code (>=0) and writes a short status into
 * `msg`. On failure returns a negative error and puts the reason in `msg`. */
int meow_elf_run_file(const char* path, int argc, char* argv[],
                      char* msg, size_t msg_len);

#ifdef __cplusplus
}
#endif
