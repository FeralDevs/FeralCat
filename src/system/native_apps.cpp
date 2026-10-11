/**
 * @file  native_apps.cpp
 * @brief See native_apps.h. Scans /apps for installed native ELF apps.
 */
#include "native_apps.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <cstring>
#include <cstdio>

static native_app_t s_apps[NATIVE_APPS_MAX];
static int          s_count = 0;

static void set_field(char* out, size_t n, const char* v)
{
    size_t L = strlen(v);
    while (L && (v[L - 1] == '\r' || v[L - 1] == '\n' || v[L - 1] == ' ')) L--;
    size_t c = L < n - 1 ? L : n - 1;
    memcpy(out, v, c);
    out[c] = '\0';
}

/* Read name= and icon= from /apps/<dir>/manifest.ini. Name falls back to the
 * directory name; icon defaults to empty (launcher picks a generic icon). */
static void read_manifest(const char* dir, native_app_t* a)
{
    strncpy(a->name, dir, sizeof(a->name) - 1);
    a->name[sizeof(a->name) - 1] = '\0';
    a->icon[0] = '\0';
    a->nes_api = 0;

    char path[80];
    snprintf(path, sizeof(path), "/apps/%s/manifest.ini", dir);
    File f = SD_MMC.open(path, FILE_READ);
    if (!f) return;

    char line[96];
    while (f.available()) {
        int len = f.readBytesUntil('\n', (uint8_t*)line, sizeof(line) - 1);
        line[len] = '\0';
        if      (strncmp(line, "name=", 5) == 0 && line[5]) set_field(a->name, sizeof(a->name), line + 5);
        else if (strncmp(line, "nes_api=", 8) == 0) {
            char value[16]; set_field(value,sizeof(value),line+8);
            // Unknown/malformed requirements fail closed, existing apps omit it.
            a->nes_api = strcmp(value,"1")==0 ? 1u : ~0u;
        }
        else if (strncmp(line, "icon=", 5) == 0 && line[5]) set_field(a->icon, sizeof(a->icon), line + 5);
    }
    f.close();
}

int native_apps_scan(void)
{
    s_count = 0;

    File root = SD_MMC.open("/apps");
    if (!root || !root.isDirectory()) { if (root) root.close(); return 0; }

    File e;
    while (s_count < NATIVE_APPS_MAX && (e = root.openNextFile())) {
        if (e.isDirectory()) {
            /* e.name() may be a full path or a basename depending on core. */
            const char* full = e.name();
            const char* base = strrchr(full, '/');
            base = base ? base + 1 : full;

            char dir[32];
            strncpy(dir, base, sizeof(dir) - 1);
            dir[sizeof(dir) - 1] = '\0';

            char elf[80], man[80];
            snprintf(elf, sizeof(elf), "/apps/%s/app.elf",      dir);
            snprintf(man, sizeof(man), "/apps/%s/manifest.ini", dir);
            if (SD_MMC.exists(elf) && SD_MMC.exists(man)) {
                strncpy(s_apps[s_count].dir, dir, sizeof(s_apps[s_count].dir) - 1);
                s_apps[s_count].dir[sizeof(s_apps[s_count].dir) - 1] = '\0';
                read_manifest(dir, &s_apps[s_count]);
                s_count++;
            }
        }
        e.close();
    }
    root.close();
    return s_count;
}

int native_apps_count(void) { return s_count; }

const native_app_t* native_apps_get(int i)
{
    return (i >= 0 && i < s_count) ? &s_apps[i] : nullptr;
}
