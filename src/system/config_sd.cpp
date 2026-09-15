/**
 * @file config_sd.cpp
 * @brief SD-card settings backup/restore implementation (see config_sd.h).
 */
#include "config_sd.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <cstring>
#include <cstdlib>
#include "persist.h"

#define CFG_DIR  "/config"
#define CFG_PATH "/config/meowkit.cfg"

/* Restore scope. */
enum { R_ALL = 0, R_WIFI = 1, R_SETTINGS = 2 };

static bool sd_ready(void)
{
    File r = SD_MMC.open("/");
    if (!r) return false;
    r.close();
    return true;
}

static bool is_wifi_key(const char* key)
{
    return !strcmp(key, "wifi_ssid") || !strcmp(key, "wifi_pass") ||
           !strcmp(key, "wifi_en");
}

void config_sd_backup(void)
{
    if (!sd_ready()) return;
    if (!SD_MMC.exists(CFG_DIR)) SD_MMC.mkdir(CFG_DIR);

    File f = SD_MMC.open(CFG_PATH, FILE_WRITE);
    if (!f) return;

    char s[80];
    f.printf("# MeowKit settings backup — restore from Settings > Backup\n");
    f.printf("bright=%d\n",    persist_get_int(PKEY_BRIGHTNESS, 50));
    f.printf("disp_to=%d\n",   persist_get_int(PKEY_DISP_TIMEOUT, 30));
    f.printf("vol=%d\n",       persist_get_int(PKEY_VOLUME, 50));
    f.printf("key_snd=%d\n",   persist_get_int(PKEY_KEY_SOUND, 1));
    f.printf("led=%d\n",       persist_get_int(PKEY_LED_BRIGHT, 50));
    f.printf("led_color=%06x\n", (unsigned)persist_get_u32(PKEY_LED_COLOR, 0xFF2A3D));
    f.printf("led_fx=%d\n",    persist_get_int(PKEY_LED_EFFECT, 1));
    f.printf("wifi_en=%d\n",   persist_get_int(PKEY_WIFI_EN, 1));
    f.printf("ble_en=%d\n",    persist_get_int(PKEY_BLE_EN, 0));

    persist_get_str(PKEY_BLE_NAME, s, sizeof(s), "");
    f.printf("ble_name=%s\n", s);
    persist_get_str(PKEY_WIFI_SSID, s, sizeof(s), "");
    f.printf("wifi_ssid=%s\n", s);
    persist_get_str(PKEY_WIFI_PASS, s, sizeof(s), "");   /* plaintext, by design */
    f.printf("wifi_pass=%s\n", s);

    f.close();
}

bool config_sd_has_backup(void)
{
    return sd_ready() && SD_MMC.exists(CFG_PATH);
}

/* Parse the backup file and apply keys permitted by `mode` into NVS. */
static void do_restore(int mode)
{
    if (!config_sd_has_backup()) return;
    File f = SD_MMC.open(CFG_PATH, FILE_READ);
    if (!f) return;

    char line[160];
    while (f.available()) {
        int n = f.readBytesUntil('\n', (uint8_t*)line, sizeof(line) - 1);
        if (n <= 0) continue;
        line[n] = '\0';
        if (line[0] == '#') continue;

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* key = line;
        char* val = eq + 1;
        size_t vl = strlen(val);
        if (vl && val[vl - 1] == '\r') val[--vl] = '\0';   /* strip CR */

        bool wifi = is_wifi_key(key);
        if (mode == R_WIFI && !wifi) continue;
        if (mode == R_SETTINGS && wifi) continue;

        if      (!strcmp(key, "bright"))    persist_set_int(PKEY_BRIGHTNESS,   atoi(val));
        else if (!strcmp(key, "disp_to"))   persist_set_int(PKEY_DISP_TIMEOUT, atoi(val));
        else if (!strcmp(key, "vol"))       persist_set_int(PKEY_VOLUME,       atoi(val));
        else if (!strcmp(key, "key_snd"))   persist_set_int(PKEY_KEY_SOUND,    atoi(val));
        else if (!strcmp(key, "led"))       persist_set_int(PKEY_LED_BRIGHT,   atoi(val));
        else if (!strcmp(key, "led_color")) persist_set_u32(PKEY_LED_COLOR, (uint32_t)strtoul(val, NULL, 16));
        else if (!strcmp(key, "led_fx"))    persist_set_int(PKEY_LED_EFFECT,   atoi(val));
        else if (!strcmp(key, "wifi_en"))   persist_set_int(PKEY_WIFI_EN,      atoi(val));
        else if (!strcmp(key, "ble_en"))    persist_set_int(PKEY_BLE_EN,       atoi(val));
        else if (!strcmp(key, "ble_name"))  persist_set_str(PKEY_BLE_NAME,     val);
        else if (!strcmp(key, "wifi_ssid")) persist_set_str(PKEY_WIFI_SSID,    val);
        else if (!strcmp(key, "wifi_pass")) persist_set_str(PKEY_WIFI_PASS,    val);
    }
    f.close();
}

void config_sd_restore(void)
{
    /* Auto-restore only when NVS looks freshly erased (no WiFi SSID). */
    char ssid[64];
    persist_get_str(PKEY_WIFI_SSID, ssid, sizeof(ssid), "");
    if (ssid[0] != '\0') return;
    if (!config_sd_has_backup()) return;
    Serial.println("[config] NVS empty — restoring settings from SD backup");
    do_restore(R_ALL);
}

void config_sd_restore_all(void)      { do_restore(R_ALL); }
void config_sd_restore_wifi(void)     { do_restore(R_WIFI); }
void config_sd_restore_settings(void) { do_restore(R_SETTINGS); }
