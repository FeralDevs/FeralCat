/**
 * @file  native_apps.h
 * @brief Catalog of native ELF apps installed on the SD card (/apps).
 *
 * A directory /apps/<dir> is a launchable native app when it contains BOTH
 * app.elf and manifest.ini (with a name= line). This keeps bare test binaries
 * (hello, hello_ns — no manifest) out of the launcher grid while real apps get
 * a tile. Signature enforcement happens at load time (see elf_runner/app_sign).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define NATIVE_APPS_MAX  12

typedef struct {
    char dir[32];    /* directory under /apps (also the launch id)     */
    char name[48];   /* display name from manifest.ini                 */
    unsigned nes_api; /* optional required NES ABI major, 0 = no requirement */
    char icon[24];   /* icon name from manifest.ini (icon=); "" = none */
} native_app_t;

/* Rescan /apps. Returns the number of native apps found. */
int native_apps_scan(void);

int                 native_apps_count(void);
const native_app_t* native_apps_get(int i);

#ifdef __cplusplus
}
#endif
