/**
 * @file  firmware_update.cpp
 * @brief See firmware_update.h. System module: WiFi-GitHub OTA, SD-card OTA,
 *        USB download mode. Ported from the former App12 (Firmware app) so the
 *        proven update logic is unchanged; only the app/Mooncake wrapper is gone.
 */
#include "firmware_update.h"
#include <Arduino.h>
#include <Update.h>
#include <SD_MMC.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "esp_system.h"
#include "esp32-hal-tinyusb.h"     /* usb_persist_restart / restart_type_t */
#include "../bsp/devices.h"
#include "../bsp/config.h"          /* MEOWGOTCHI_FW_VERSION */
#include "../app/app_common/mk_tui.h"
#include "persist.h"

/* GitHub repo that hosts the releases. "latest" always resolves to the newest
 * published release, so these URLs never need bumping per version. */
#define OTA_REPO      "FeralDevs/FeralCat"
#define OTA_LATEST    "https://github.com/" OTA_REPO "/releases/latest"
#define OTA_FIRMWARE  OTA_LATEST "/download/firmware.bin"
#define OTA_UA        "MeowGotchi-OTA"

#define FWU_ROWS 3

/* Active device for the current takeover (set in firmware_update_run). A single
 * updater runs at a time, so file-scope is safe and keeps the helpers tidy. */
static DEVICES* s_dev = nullptr;

/* Pending-request flag, set by the Settings button, drained by the launcher. */
static volatile bool s_pending = false;

/* ── Screens ─────────────────────────────────────────────────────────────── */

static void fwu_draw_menu(int sel)
{
    LGFX_Class& lcd = s_dev->Lcd;
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "Firmware");
    MK_TUI::drawMenuItem(lcd, 0, "Update over WiFi", "from GitHub",  sel == 0);
    MK_TUI::drawMenuItem(lcd, 1, "Update from SD",   "firmware.bin", sel == 1);
    MK_TUI::drawMenuItem(lcd, 2, "USB Download Mode", "flash via PC", sel == 2);
    MK_TUI::drawFooter(lcd, "Select", "Exit");
}

/* Blocking modal: show a message, wait for A/B, then return. */
static void fwu_modal(const char* line1, const char* line2, uint32_t color)
{
    LGFX_Class& lcd = s_dev->Lcd;
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "Firmware");
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor(color, (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, 100);
    lcd.printf("%s", line1);
    if (line2 && line2[0]) {
        lcd.setTextColor((uint32_t)MK_PAL::TEXT_SEC, (uint32_t)MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::PAD, 122);
        lcd.printf("%s", line2);
    }
    MK_TUI::drawFooter(lcd, "OK", "");
    while (true) {
        s_dev->button.update();
        s_dev->button.tick();
        if (s_dev->button.A.pressed() || s_dev->button.B.pressed()) break;
        delay(20);
    }
}

/* One centred status line under the header — used for the connect/check steps. */
static void fwu_status(const char* line, uint32_t color)
{
    LGFX_Class& lcd = s_dev->Lcd;
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "WiFi Update");
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor(color, (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, 110);
    lcd.printf("%s", line);
    MK_TUI::drawFooter(lcd, "", "Cancel");
}

/* Two-line Yes/No prompt. A = true, B = false. */
static bool fwu_confirm(const char* line1, const char* line2,
                        const char* btnA, const char* btnB)
{
    LGFX_Class& lcd = s_dev->Lcd;
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "WiFi Update");
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor((uint32_t)MK_PAL::TEXT_PRI, (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, 96);
    lcd.printf("%s", line1);
    lcd.setTextColor((uint32_t)MK_PAL::ACCENT, (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, 118);
    lcd.printf("%s", line2);
    MK_TUI::drawFooter(lcd, btnA, btnB);
    while (true) {
        s_dev->button.update();
        s_dev->button.tick();
        if (s_dev->button.A.pressed()) return true;
        if (s_dev->button.B.pressed()) return false;
        delay(20);
    }
}

/* ── USB download mode ───────────────────────────────────────────────────── */

static void fwu_usb_download()
{
    LGFX_Class& lcd = s_dev->Lcd;
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "Firmware");
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor((uint32_t)MK_PAL::ACCENT, (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, 110);
    lcd.printf("Entering download mode...");
    delay(600);
    usb_persist_restart(RESTART_BOOTLOADER);   /* does not return */
    esp_restart();
}

/* ── SD-card OTA ─────────────────────────────────────────────────────────── */

static void fwu_sd_update()
{
    LGFX_Class& lcd = s_dev->Lcd;

    /* Robust SD probe (same approach as ui_sd_present): open "/", remount on
     * failure. The launcher mounts the global SD_MMC, so use that directly. */
    File root = SD_MMC.open("/");
    if (!root) {
        SD_MMC.end();
        if (!SD_MMC.begin("/sdcard", true, false, 10000) || !(root = SD_MMC.open("/"))) {
            fwu_modal("No SD card", "Insert a card and retry", MK_PAL::ERR);
            return;
        }
    }
    root.close();

    File f = SD_MMC.open("/firmware.bin", FILE_READ);
    if (!f || f.isDirectory()) { if (f) f.close();
        fwu_modal("firmware.bin not found", "Copy it to the SD root", MK_PAL::ERR); return; }

    size_t sz = f.size();
    if (sz < 0x10000) { f.close(); fwu_modal("Image too small", "Not a valid firmware.bin", MK_PAL::ERR); return; }
    if (!Update.begin(sz)) { f.close(); fwu_modal("Update init failed", Update.errorString(), MK_PAL::ERR); return; }

    /* Static progress screen (only the bar/percent update each step). */
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "SD Update");
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor((uint32_t)MK_PAL::TEXT_PRI, (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, MK_LAYOUT::CONTENT_Y + 16);
    lcd.printf("Writing firmware...");
    lcd.setTextColor((uint32_t)MK_PAL::ERR, (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, MK_LAYOUT::CONTENT_Y + 38);
    lcd.printf("Do NOT power off.");

    static uint8_t buf[4096];
    size_t done = 0;
    int last = -1;
    while (f.available()) {
        int n = f.read(buf, sizeof(buf));
        if (n <= 0) break;
        if ((int)Update.write(buf, n) != n) {
            f.close(); Update.abort();
            fwu_modal("Write error", Update.errorString(), MK_PAL::ERR);
            return;
        }
        done += n;
        int pct = (int)(done * 100 / sz);
        if (pct != last) {
            last = pct;
            MK_TUI::drawProgress(lcd, MK_LAYOUT::CONTENT_Y + 80, pct, nullptr);
            lcd.setFont(&fonts::efontCN_16);
            lcd.setTextColor((uint32_t)MK_PAL::ACCENT, (uint32_t)MK_PAL::BLACK);
            lcd.setCursor(MK_LAYOUT::W - 60, MK_LAYOUT::CONTENT_Y + 96);
            lcd.printf("%3d%%", pct);
        }
    }
    f.close();

    if (Update.end(true)) { fwu_modal("Update complete", "Rebooting...", MK_PAL::OK); delay(300); esp_restart(); }
    else                  { fwu_modal("Update failed", Update.errorString(), MK_PAL::ERR); }
}

/* ── WiFi-GitHub OTA ─────────────────────────────────────────────────────── */

/* Join the network saved in Settings. Blocks (with a Cancel) until connected,
 * timed out, or the user backs out. */
static bool fwu_wifi_connect()
{
    char ssid[33] = {};
    char pass[65] = {};
    persist_get_str(PKEY_WIFI_SSID, ssid, sizeof(ssid), "");
    persist_get_str(PKEY_WIFI_PASS, pass, sizeof(pass), "");
    if (ssid[0] == '\0') {
        fwu_modal("No saved WiFi", "Connect in Settings first", MK_PAL::ERR);
        return false;
    }

    if (s_dev->wifi.isConnected()) return true;

    fwu_status("Connecting WiFi...", MK_PAL::ACCENT);
    s_dev->wifi.begin();
    s_dev->wifi.connect(ssid, pass, 20000);

    while (true) {
        s_dev->wifi.update();
        if (s_dev->wifi.isConnected()) return true;

        WiFi_Class::State st = s_dev->wifi.getState();
        if (st == WiFi_Class::State::FAILED) {
            fwu_modal("WiFi failed", ssid, MK_PAL::ERR);
            return false;
        }
        s_dev->button.update();
        s_dev->button.tick();
        if (s_dev->button.B.pressed()) { s_dev->wifi.disconnect(); return false; }
        delay(50);
    }
}

/* Read the latest release tag from GitHub's redirect on /releases/latest. */
static bool fwu_fetch_latest_tag(char* out, size_t n)
{
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setUserAgent(OTA_UA);
    http.setConnectTimeout(10000);
    http.setTimeout(10000);
    if (!http.begin(client, OTA_LATEST)) return false;
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    const char* hdrs[] = { "Location" };
    http.collectHeaders(hdrs, 1);

    int code = http.GET();
    String loc = http.header("Location");
    http.end();

    /* 302 → .../releases/tag/<tag> ; 200 with no Location means no releases. */
    if (code != HTTP_CODE_FOUND && code != HTTP_CODE_MOVED_PERMANENTLY) return false;
    int slash = loc.lastIndexOf('/');
    if (slash < 0 || slash + 1 >= (int)loc.length()) return false;
    String tag = loc.substring(slash + 1);
    tag.trim();
    if (tag.isEmpty()) return false;
    strncpy(out, tag.c_str(), n - 1);
    out[n - 1] = '\0';
    return true;
}

/* Stream the latest firmware.bin to the SD card root with a progress bar. */
static bool fwu_download_to_sd(const char* tag)
{
    /* Make sure the SD is mounted (launcher mounts the global SD_MMC). */
    File probe = SD_MMC.open("/");
    if (!probe) {
        SD_MMC.end();
        if (!SD_MMC.begin("/sdcard", true, false, 10000) || !(probe = SD_MMC.open("/"))) {
            fwu_modal("No SD card", "Insert a card and retry", MK_PAL::ERR);
            return false;
        }
    }
    probe.close();

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setUserAgent(OTA_UA);
    http.setConnectTimeout(10000);
    http.setTimeout(15000);
    if (!http.begin(client, OTA_FIRMWARE)) { fwu_modal("Download failed", "Connect error", MK_PAL::ERR); return false; }
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);   /* → objects.githubusercontent.com */

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        char msg[24]; snprintf(msg, sizeof(msg), "HTTP %d", code);
        fwu_modal("Download failed", msg, MK_PAL::ERR);
        return false;
    }

    int total = http.getSize();          /* -1 if unknown */
    if (total > 0 && total < 0x10000) {  /* way too small to be a real image */
        http.end(); fwu_modal("Bad image size", "Aborting", MK_PAL::ERR); return false;
    }

    /* Download to a temp name, then rename — a half-written /firmware.bin must
     * never be left where the SD flasher would pick it up. */
    SD_MMC.remove("/firmware.bin.part");
    File f = SD_MMC.open("/firmware.bin.part", FILE_WRITE);
    if (!f) { http.end(); fwu_modal("SD write failed", "Cannot open file", MK_PAL::ERR); return false; }

    LGFX_Class& lcd = s_dev->Lcd;
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "WiFi Update");
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor((uint32_t)MK_PAL::TEXT_PRI, (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, MK_LAYOUT::CONTENT_Y + 16);
    lcd.printf("Downloading %s", tag);

    WiFiClient* stream = http.getStreamPtr();
    static uint8_t buf[4096];
    int done = 0, last = -1;
    bool ok = true;
    uint32_t idle_since = millis();
    while (http.connected() && (total < 0 || done < total)) {
        size_t avail = stream->available();
        if (avail) {
            int n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
            if (n <= 0) break;
            if ((int)f.write(buf, n) != n) { ok = false; break; }
            done += n;
            idle_since = millis();
            if (total > 0) {
                int pct = (int)((int64_t)done * 100 / total);
                if (pct != last) {
                    last = pct;
                    MK_TUI::drawProgress(lcd, MK_LAYOUT::CONTENT_Y + 80, pct, nullptr);
                    lcd.setFont(&fonts::efontCN_16);
                    lcd.setTextColor((uint32_t)MK_PAL::ACCENT, (uint32_t)MK_PAL::BLACK);
                    lcd.setCursor(MK_LAYOUT::W - 60, MK_LAYOUT::CONTENT_Y + 96);
                    lcd.printf("%3d%%", pct);
                }
            }
        } else {
            if (millis() - idle_since > 15000) { ok = false; break; }   /* stalled */
            delay(10);
        }
    }
    f.close();
    http.end();

    if (!ok || (total > 0 && done < total)) {
        SD_MMC.remove("/firmware.bin.part");
        fwu_modal("Download incomplete", "Check WiFi and retry", MK_PAL::ERR);
        return false;
    }

    SD_MMC.remove("/firmware.bin");
    if (!SD_MMC.rename("/firmware.bin.part", "/firmware.bin")) {
        SD_MMC.remove("/firmware.bin.part");
        fwu_modal("SD write failed", "Rename error", MK_PAL::ERR);
        return false;
    }
    return true;
}

static void fwu_wifi_update()
{
    if (!fwu_wifi_connect()) return;

    fwu_status("Checking GitHub...", MK_PAL::ACCENT);
    char tag[24] = {};
    if (!fwu_fetch_latest_tag(tag, sizeof(tag))) {
        fwu_modal("Update check failed", "No release found", MK_PAL::ERR);
        s_dev->wifi.disconnect();
        return;
    }

    /* Same tag as this build → nothing to do. */
    if (strcmp(tag, MEOWGOTCHI_FW_VERSION) == 0) {
        fwu_modal("Up to date", tag, MK_PAL::OK);
        s_dev->wifi.disconnect();
        return;
    }

    /* Offer the download. */
    char verline[48];
    snprintf(verline, sizeof(verline), "%s  ->  %s", MEOWGOTCHI_FW_VERSION, tag);
    if (!fwu_confirm("New version available", verline, "Download", "Cancel")) {
        s_dev->wifi.disconnect();
        return;
    }

    bool got = fwu_download_to_sd(tag);
    s_dev->wifi.disconnect();
    if (!got) return;

    /* Downloaded to SD. Offer to flash it now via the SD-OTA path. */
    char doneline[32];
    snprintf(doneline, sizeof(doneline), "Downloaded %s", tag);
    if (fwu_confirm(doneline, "Flash it now?", "Update", "Later"))
        fwu_sd_update();   /* reboots on success */
}

/* ── Public API ──────────────────────────────────────────────────────────── */

extern "C" void firmware_update_request(void) { s_pending = true; }

extern "C" int firmware_update_take_pending(void)
{
    if (!s_pending) return 0;
    s_pending = false;
    return 1;
}

void firmware_update_run(DEVICES* dev)
{
    if (!dev) return;
    s_dev = dev;

    int  sel   = 0;
    bool dirty = true;
    while (true) {
        dev->button.update();
        dev->button.tick();

        /* B (short or long) exits back to the caller. */
        if (dev->button.B.pressed() || dev->button.B.isLongPress()) break;

        if (dev->button.Up.pressed())   { sel = (sel + FWU_ROWS - 1) % FWU_ROWS; dirty = true; }
        if (dev->button.Down.pressed()) { sel = (sel + 1) % FWU_ROWS;            dirty = true; }
        if (dev->button.A.pressed()) {
            switch (sel) {
                case 0:  fwu_wifi_update();  break;
                case 1:  fwu_sd_update();    break;
                default: fwu_usb_download(); break;
            }
            dirty = true;   /* redraw the menu after a sub-flow returns */
        }

        if (dirty) { fwu_draw_menu(sel); dirty = false; }
        delay(20);
    }

    s_dev = nullptr;
}
