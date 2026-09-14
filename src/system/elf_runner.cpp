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

int meow_elf_run_file(const char* path, int argc, char* argv[],
                      char* msg, size_t msg_len)
{
    auto say = [&](const char* s) { if (msg && msg_len) { strncpy(msg, s, msg_len - 1); msg[msg_len - 1] = '\0'; } };

    File f = SD_MMC.open(path, FILE_READ);
    if (!f) { say("open failed"); Serial.printf("[elf] open %s failed\n", path); return -1; }
    size_t sz = f.size();
    if (sz == 0 || sz > 512 * 1024) { f.close(); say("bad size"); return -2; }

    /* ELF image buffer: PSRAM is fine for the *file bytes* (the loader copies
     * executable segments into exec-capable RAM itself). */
    uint8_t* buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (uint8_t*)malloc(sz);
    if (!buf) { f.close(); say("out of memory"); return -3; }
    size_t rd = f.read(buf, sz);
    f.close();
    if (rd != sz) { free(buf); say("read short"); return -4; }

    Serial.printf("[elf] %s: %u bytes, loading...\n", path, (unsigned)sz);

    esp_elf_t elf;
    int r = esp_elf_init(&elf);
    if (r != 0) { free(buf); say("init failed"); Serial.printf("[elf] init=%d\n", r); return -5; }

    r = esp_elf_relocate(&elf, buf);
    if (r != 0) { esp_elf_deinit(&elf); free(buf); say("relocate failed");
                  Serial.printf("[elf] relocate=%d\n", r); return -6; }

    Serial.println("[elf] running app_main...");
    int rc = esp_elf_request(&elf, 0, argc, argv);
    Serial.printf("[elf] app returned %d\n", rc);

    esp_elf_deinit(&elf);
    free(buf);

    char b[48];
    snprintf(b, sizeof(b), "ran ok, returned %d", rc);
    say(b);
    return rc;
}
