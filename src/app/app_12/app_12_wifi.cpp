/**
 * @file  app_12_wifi.cpp
 * @brief Firmware app — "Update over WiFi": pull the latest release's
 *        firmware.bin from GitHub onto the SD card, then offer to flash it.
 *
 * Flow (App12::_wifiUpdate):
 *   1. Reconnect to the WiFi saved in Settings (NVS wifi_ssid/wifi_pass).
 *   2. GET <repo>/releases/latest with redirects off → read the tag from the
 *      Location header (no JSON, no api.github.com rate limit).
 *   3. If the tag differs from this build, stream
 *      <repo>/releases/latest/download/firmware.bin → /firmware.bin on the SD.
 *   4. Offer "Update now?" → hand off to the existing SD‑OTA flasher.
 *
 * TLS uses setInsecure() (no cert pinning). The image is still verified by the
 * OTA layer on flash, and a bad write safely rolls back to the current slot.
 */
#include "app_12.h"
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SD_MMC.h>
#include "../app_common/mk_tui.h"
#include "../../system/persist.h"

namespace MOONCAKE::APPS
{

/* GitHub repo that hosts the releases. "latest" always resolves to the newest
 * published release, so these URLs never need bumping per version. */
#define OTA_REPO      "janud/MeowKitCustomFW"
#define OTA_LATEST    "https://github.com/" OTA_REPO "/releases/latest"
#define OTA_FIRMWARE  OTA_LATEST "/download/firmware.bin"
#define OTA_UA        "MeowGotchi-OTA"

/* One centred status line under the header — used for the connect/check steps. */
void App12::_statusScreen(const char* line, uint32_t color)
{
    LGFX_Class& lcd = _device->Lcd;
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "WiFi Update");
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor(color, (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, 110);
    lcd.printf("%s", line);
    MK_TUI::drawFooter(lcd, "", "Cancel");
}

/* Two‑line Yes/No prompt. A = true, B = false. */
bool App12::_confirm(const char* line1, const char* line2,
                     const char* btnA, const char* btnB)
{
    LGFX_Class& lcd = _device->Lcd;
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
        _device->button.update();
        _device->button.tick();
        if (_device->button.A.pressed()) return true;
        if (_device->button.B.pressed()) return false;
        delay(20);
    }
}

/* Join the network saved in Settings. Blocks (with a Cancel) until connected,
 * timed out, or the user backs out. */
bool App12::_wifiConnect()
{
    char ssid[33] = {};
    char pass[65] = {};
    persist_get_str(PKEY_WIFI_SSID, ssid, sizeof(ssid), "");
    persist_get_str(PKEY_WIFI_PASS, pass, sizeof(pass), "");
    if (ssid[0] == '\0') {
        _modal("No saved WiFi", "Connect in Settings first", MK_PAL::ERR);
        return false;
    }

    if (_device->wifi.isConnected()) return true;

    _statusScreen("Connecting WiFi...", MK_PAL::ACCENT);
    _device->wifi.begin();
    _device->wifi.connect(ssid, pass, 20000);

    while (true) {
        _device->wifi.update();
        if (_device->wifi.isConnected()) return true;

        WiFi_Class::State st = _device->wifi.getState();
        if (st == WiFi_Class::State::FAILED) {
            _modal("WiFi failed", ssid, MK_PAL::ERR);
            return false;
        }
        _device->button.update();
        _device->button.tick();
        if (_device->button.B.pressed()) { _device->wifi.disconnect(); return false; }
        delay(50);
    }
}

/* Read the latest release tag from GitHub's redirect on /releases/latest. */
bool App12::_fetchLatestTag(char* out, size_t n)
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
bool App12::_downloadToSD(const char* tag)
{
    /* Make sure the SD is mounted (launcher mounts the global SD_MMC). */
    File probe = SD_MMC.open("/");
    if (!probe) {
        SD_MMC.end();
        if (!SD_MMC.begin("/sdcard", true, false, 10000) || !(probe = SD_MMC.open("/"))) {
            _modal("No SD card", "Insert a card and retry", MK_PAL::ERR);
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
    if (!http.begin(client, OTA_FIRMWARE)) { _modal("Download failed", "Connect error", MK_PAL::ERR); return false; }
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);   /* → objects.githubusercontent.com */

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        char msg[24]; snprintf(msg, sizeof(msg), "HTTP %d", code);
        _modal("Download failed", msg, MK_PAL::ERR);
        return false;
    }

    int total = http.getSize();          /* -1 if unknown */
    if (total > 0 && total < 0x10000) {  /* way too small to be a real image */
        http.end(); _modal("Bad image size", "Aborting", MK_PAL::ERR); return false;
    }

    /* Download to a temp name, then rename — a half-written /firmware.bin must
     * never be left where the SD flasher would pick it up. */
    SD_MMC.remove("/firmware.bin.part");
    File f = SD_MMC.open("/firmware.bin.part", FILE_WRITE);
    if (!f) { http.end(); _modal("SD write failed", "Cannot open file", MK_PAL::ERR); return false; }

    LGFX_Class& lcd = _device->Lcd;
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
        _modal("Download incomplete", "Check WiFi and retry", MK_PAL::ERR);
        return false;
    }

    SD_MMC.remove("/firmware.bin");
    if (!SD_MMC.rename("/firmware.bin.part", "/firmware.bin")) {
        SD_MMC.remove("/firmware.bin.part");
        _modal("SD write failed", "Rename error", MK_PAL::ERR);
        return false;
    }
    return true;
}

void App12::_wifiUpdate()
{
    if (!_wifiConnect()) { _dirty = true; return; }

    _statusScreen("Checking GitHub...", MK_PAL::ACCENT);
    char tag[24] = {};
    if (!_fetchLatestTag(tag, sizeof(tag))) {
        _modal("Update check failed", "No release found", MK_PAL::ERR);
        _device->wifi.disconnect();
        _dirty = true;
        return;
    }

    /* Same tag as this build → nothing to do. */
    if (strcmp(tag, MEOWGOTCHI_FW_VERSION) == 0) {
        _modal("Up to date", tag, MK_PAL::OK);
        _device->wifi.disconnect();
        _dirty = true;
        return;
    }

    /* Offer the download. */
    char verline[48];
    snprintf(verline, sizeof(verline), "%s  ->  %s", MEOWGOTCHI_FW_VERSION, tag);
    if (!_confirm("New version available", verline, "Download", "Cancel")) {
        _device->wifi.disconnect();
        _dirty = true;
        return;
    }

    bool got = _downloadToSD(tag);
    _device->wifi.disconnect();
    if (!got) { _dirty = true; return; }

    /* Downloaded to SD. Offer to flash it now via the existing SD‑OTA path. */
    char doneline[32];
    snprintf(doneline, sizeof(doneline), "Downloaded %s", tag);
    if (_confirm(doneline, "Flash it now?", "Update", "Later"))
        _sdUpdate();   /* reboots on success */
    else
        _dirty = true;
}

} // namespace MOONCAKE::APPS
