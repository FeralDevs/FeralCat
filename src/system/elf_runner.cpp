/**
 * @file elf_runner.cpp
 * @brief See elf_runner.h. Spike wrapper around lib/elf_loader.
 */
#include "elf_runner.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <cstring>
#include <cstdlib>
#include "esp_elf.h"
#include "persist.h"
#include "app_sign.h"
#include "mk_nes_abi.h"

/* Crash breadcrumb: written to NVS before each risky step so that, after a hard
 * fault reboots the device, ELF Test can show the last step reached. */
enum { ELF_ST_NONE = 0, ELF_ST_OPENED, ELF_ST_READ, ELF_ST_INIT,
       ELF_ST_RELOCATE, ELF_ST_RUN, ELF_ST_DONE };
static void elf_breadcrumb(int stage) { persist_set_int(PKEY_ELF_STAGE, stage); }

int meow_elf_run_file(const char* path, int argc, char* argv[],
                      char* msg, size_t msg_len)
{
    auto say = [&](const char* s) { if (msg && msg_len) { strncpy(msg, s, msg_len - 1); msg[msg_len - 1] = '\0'; } };
    elf_breadcrumb(ELF_ST_NONE);

    File f = SD_MMC.open(path, FILE_READ);
    if (!f) { say("open failed"); Serial.printf("[elf] open %s failed\n", path); return -1; }
    size_t sz = f.size();
    if (sz == 0 || sz > 512 * 1024) { f.close(); say("bad size"); return -2; }

    /* ELF image buffer: PSRAM is fine for the *file bytes* (the loader copies
     * executable segments into exec-capable RAM itself). */
    uint8_t* buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (uint8_t*)malloc(sz);
    if (!buf) { f.close(); say("out of memory"); return -3; }
    elf_breadcrumb(ELF_ST_OPENED);
    size_t rd = f.read(buf, sz);
    f.close();
    if (rd != sz) { free(buf); say("read short"); return -4; }
    elf_breadcrumb(ELF_ST_READ);

    /* Signature gate: apps carry a detached 64-byte Ed25519 sig at "<path>.sig".
     * A valid signature always runs; an unsigned or tampered app runs only when
     * the user has enabled "Allow unsigned apps" in Settings ▸ Features. */
    bool sig_ok = false;
    {
        char sigpath[128];
        snprintf(sigpath, sizeof(sigpath), "%s.sig", path);
        File sf = SD_MMC.open(sigpath, FILE_READ);
        if (sf) {
            uint8_t sig[64];
            if (sf.size() == 64 && sf.read(sig, 64) == 64)
                sig_ok = meow_app_verify(buf, sz, sig, sizeof(sig));
            sf.close();
        }
    }
    if (!sig_ok) {
        if (persist_get_int(PKEY_ELF_UNSIGNED, 0) == 0) {
            free(buf);
            say("unsigned - enable in Settings");
            Serial.printf("[elf] %s blocked: no valid signature\n", path);
            return -7;
        }
        Serial.printf("[elf] WARNING: running unsigned app %s\n", path);
    }

    Serial.printf("[elf] %s: %u bytes, %s, loading...\n",
                  path, (unsigned)sz, sig_ok ? "signed" : "UNSIGNED");

    esp_elf_t elf;
    elf_breadcrumb(ELF_ST_INIT);
    int r = esp_elf_init(&elf);
    if (r != 0) { free(buf); say("init failed"); Serial.printf("[elf] init=%d\n", r); return -5; }

    elf_breadcrumb(ELF_ST_RELOCATE);
    r = esp_elf_relocate(&elf, buf);
    if (r != 0) { esp_elf_deinit(&elf); free(buf); say("relocate failed");
                  Serial.printf("[elf] relocate=%d\n", r); return -6; }

    Serial.println("[elf] running app_main...");
    elf_breadcrumb(ELF_ST_RUN);
    /* esp_elf_request() discards the entry's return value (always returns 0),
     * so call the relocated entry directly to surface app_main's real result. */
    int rc = elf.entry ? elf.entry(argc, argv) : -100;
    // Service workers and peripheral ownership must end before app code unloads.
    mk_nes_end();
    elf_breadcrumb(ELF_ST_DONE);
    Serial.printf("[elf] app returned %d\n", rc);

    esp_elf_deinit(&elf);
    free(buf);

    char b[48];
    snprintf(b, sizeof(b), "ran ok, returned %d", rc);
    say(b);
    return rc;
}
