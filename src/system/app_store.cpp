/**
 * @file  app_store.cpp
 * @brief App Store system module — see app_store.h.
 *
 * Flow: connect saved WiFi → GET catalog.json → parse → browse list → on
 * select, download app.elf/.sig/manifest.ini to /apps/<id>/*.part → verify the
 * Ed25519 signature on-device → move into place → rescan the launcher catalog.
 * Modeled on firmware_update (blocking LovyanGFX takeover, direct button reads).
 */
#include "app_store.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include "../bsp/devices.h"
#include "../bsp/config.h"
#include "../app/app_common/mk_tui.h"
#include "persist.h"
#include "app_sign.h"
#include "native_apps.h"

/* Curated catalog hosted on the FeralCat Installer Pages site (same origin as
 * the app files it references). */
static const char* AS_CATALOG = "https://feraldevs.github.io/FeralCat-Installer/apps/catalog.json";
static const char* AS_BASE    = "https://feraldevs.github.io/FeralCat-Installer/";

static DEVICES*      s_dev     = nullptr;
static volatile bool s_pending = false;

extern "C" void app_store_request(void)      { s_pending = true; }
extern "C" int  app_store_take_pending(void) { int p = s_pending; s_pending = false; return p; }

/* ── Catalog model ──────────────────────────────────────────────────────── */
#define AS_MAX_APPS 16
enum { AS_NEW = 0, AS_INSTALLED, AS_UPDATE };
struct StoreApp {
    char id[32], name[48], author[40], version[16], icon[24];
    char elf[128], sig[128], manifest[128];   /* relative paths from the catalog */
    uint32_t size;
    int  state;
};
static StoreApp s_apps[AS_MAX_APPS];
static int      s_napps = 0;

/* ── UI helpers (mirrors firmware_update) ───────────────────────────────── */
static void as_status(const char* line, uint32_t color)
{
    LGFX_Class& lcd = s_dev->Lcd;
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "App Store");
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor(color, (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, 110);
    lcd.printf("%s", line);
    MK_TUI::drawFooter(lcd, "", "Cancel");
}

static void as_modal(const char* line1, const char* line2, uint32_t color)
{
    LGFX_Class& lcd = s_dev->Lcd;
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "App Store");
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
        s_dev->button.update(); s_dev->button.tick();
        if (s_dev->button.A.pressed() || s_dev->button.B.pressed()) break;
        delay(20);
    }
}

static bool as_confirm(const char* line1, const char* line2, const char* btnA, const char* btnB)
{
    LGFX_Class& lcd = s_dev->Lcd;
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "App Store");
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor((uint32_t)MK_PAL::TEXT_PRI, (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, 96);  lcd.printf("%s", line1);
    lcd.setTextColor((uint32_t)MK_PAL::ACCENT, (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, 118); lcd.printf("%s", line2);
    MK_TUI::drawFooter(lcd, btnA, btnB);
    while (true) {
        s_dev->button.update(); s_dev->button.tick();
        if (s_dev->button.A.pressed()) return true;
        if (s_dev->button.B.pressed()) return false;
        delay(20);
    }
}

/* ── WiFi (mirrors firmware_update) ─────────────────────────────────────── */
static bool as_wifi_connect()
{
    char ssid[33] = {}, pass[65] = {};
    persist_get_str(PKEY_WIFI_SSID, ssid, sizeof(ssid), "");
    persist_get_str(PKEY_WIFI_PASS, pass, sizeof(pass), "");
    if (ssid[0] == '\0') { as_modal("No saved WiFi", "Connect in Settings first", MK_PAL::ERR); return false; }
    if (s_dev->wifi.isConnected()) return true;
    as_status("Connecting WiFi...", MK_PAL::ACCENT);
    s_dev->wifi.begin();
    s_dev->wifi.connect(ssid, pass, 20000);
    while (true) {
        s_dev->wifi.update();
        if (s_dev->wifi.isConnected()) return true;
        if (s_dev->wifi.getState() == WiFi_Class::State::FAILED) { as_modal("WiFi failed", ssid, MK_PAL::ERR); return false; }
        s_dev->button.update(); s_dev->button.tick();
        if (s_dev->button.B.pressed()) { s_dev->wifi.disconnect(); return false; }
        delay(50);
    }
}

/* ── HTTP: GET into a RAM buffer (catalog). Returns bytes read, or <0. ──── */
static int as_http_get(const char* url, uint8_t* buf, size_t maxlen)
{
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http; http.setConnectTimeout(8000); http.setTimeout(12000);
    if (!http.begin(client, url)) return -1;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (http.GET() != HTTP_CODE_OK) { http.end(); return -2; }
    int total = http.getSize();
    WiFiClient* stream = http.getStreamPtr();
    size_t done = 0; uint32_t idle = millis();
    while (http.connected() && (total < 0 || done < (size_t)total) && done < maxlen) {
        size_t avail = stream->available();
        if (avail) {
            size_t want = maxlen - done; if (avail < want) want = avail;
            int n = stream->readBytes(buf + done, want);
            if (n <= 0) break;
            done += n; idle = millis();
        } else { if (millis() - idle > 12000) break; delay(10); }
    }
    http.end();
    return (int)done;
}

/* ── HTTP: GET streamed to an SD file. ──────────────────────────────────── */
static bool as_http_get_to_file(const char* url, const char* path)
{
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http; http.setConnectTimeout(8000); http.setTimeout(12000);
    if (!http.begin(client, url)) return false;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (http.GET() != HTTP_CODE_OK) { http.end(); return false; }
    int total = http.getSize();
    File f = SD_MMC.open(path, FILE_WRITE);
    if (!f) { http.end(); return false; }
    WiFiClient* stream = http.getStreamPtr();
    static uint8_t buf[2048];
    size_t done = 0; uint32_t idle = millis(); bool ok = true;
    while (http.connected() && (total < 0 || done < (size_t)total)) {
        size_t avail = stream->available();
        if (avail) {
            int n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
            if (n <= 0) break;
            if ((int)f.write(buf, n) != n) { ok = false; break; }
            done += n; idle = millis();
        } else { if (millis() - idle > 15000) { ok = false; break; } delay(10); }
    }
    f.close(); http.end();
    if (!ok || (total > 0 && done < (size_t)total)) { SD_MMC.remove(path); return false; }
    return true;
}

/* ── Minimal catalog JSON parsing (our fixed, trusted shape). ───────────── */
static bool json_field(const char* o, const char* end, const char* key, char* out, size_t n)
{
    out[0] = 0;
    const char* k = strstr(o, key);
    if (!k || k >= end) return false;
    const char* p = k + strlen(key);
    while (p < end && *p != ':') ++p;        /* skip to ':' */
    while (p < end && (*p == ':' || *p == ' ' || *p == '\t')) ++p;
    if (p >= end || *p != '"') return false; /* expect a string value */
    ++p;
    size_t i = 0;
    while (p < end && *p != '"' && i + 1 < n) {
        if (*p == '\\' && p + 1 < end) ++p;   /* keep escaped char literally */
        out[i++] = *p++;
    }
    out[i] = 0;
    return true;
}
static long json_int(const char* o, const char* end, const char* key)
{
    const char* k = strstr(o, key);
    if (!k || k >= end) return -1;
    const char* p = k + strlen(key);
    while (p < end && (*p < '0' || *p > '9')) ++p;
    return (p < end) ? atol(p) : -1;
}

static int as_parse_catalog(const char* json, int len)
{
    int count = 0;
    const char* end = json + len;
    const char* p = strstr(json, "\"apps\"");
    if (!p) return 0;
    p = strchr(p, '[');
    if (!p) return 0;
    ++p;
    while (p < end && count < AS_MAX_APPS) {
        /* find next object '{' */
        while (p < end && *p != '{' && *p != ']') ++p;
        if (p >= end || *p == ']') break;
        const char* obj = p;
        int depth = 0; const char* q = p;
        for (; q < end; ++q) { if (*q == '{') ++depth; else if (*q == '}') { if (--depth == 0) { ++q; break; } } }
        const char* oend = q;   /* one past the object's closing '}' */

        StoreApp& a = s_apps[count];
        memset(&a, 0, sizeof(a));
        json_field(obj, oend, "\"id\"",       a.id,       sizeof(a.id));
        json_field(obj, oend, "\"name\"",     a.name,     sizeof(a.name));
        json_field(obj, oend, "\"author\"",   a.author,   sizeof(a.author));
        json_field(obj, oend, "\"version\"",  a.version,  sizeof(a.version));
        json_field(obj, oend, "\"icon\"",     a.icon,     sizeof(a.icon));
        json_field(obj, oend, "\"elf\"",      a.elf,      sizeof(a.elf));
        json_field(obj, oend, "\"sig\"",      a.sig,      sizeof(a.sig));
        json_field(obj, oend, "\"manifest\"", a.manifest, sizeof(a.manifest));
        long sz = json_int(obj, oend, "\"size\"");
        a.size = sz > 0 ? (uint32_t)sz : 0;
        a.state = AS_NEW;
        if (a.id[0] && a.elf[0] && a.sig[0] && a.manifest[0]) ++count;
        p = oend;
    }
    return count;
}

/* Read an installed app's version= line (empty if not installed). */
static void installed_version(const char* id, char* out, size_t n)
{
    out[0] = 0;
    char path[80];
    snprintf(path, sizeof(path), "/apps/%s/manifest.ini", id);
    File f = SD_MMC.open(path, FILE_READ);
    if (!f) return;
    char line[64];
    while (f.available()) {
        int len = f.readBytesUntil('\n', line, sizeof(line) - 1);
        line[len] = 0;
        if (!strncmp(line, "version=", 8)) {
            strncpy(out, line + 8, n - 1); out[n - 1] = 0;
            char* nl = strpbrk(out, "\r\n"); if (nl) *nl = 0;
            break;
        }
    }
    f.close();
}

static void as_mark_states()
{
    char ver[16];
    for (int i = 0; i < s_napps; ++i) {
        installed_version(s_apps[i].id, ver, sizeof(ver));
        if (!ver[0])                               s_apps[i].state = AS_NEW;
        else if (!strcmp(ver, s_apps[i].version))  s_apps[i].state = AS_INSTALLED;
        else                                       s_apps[i].state = AS_UPDATE;
    }
}

/* ── Install: download → verify signature → place. ──────────────────────── */
static bool as_install(const StoreApp& a)
{
    char dir[48], url[256];
    char elf_p[80], sig_p[80], man_p[80], elf_f[64], sig_f[64], man_f[64];
    snprintf(dir, sizeof(dir), "/apps/%s", a.id);
    if (!SD_MMC.exists("/apps")) SD_MMC.mkdir("/apps");
    if (!SD_MMC.exists(dir))     SD_MMC.mkdir(dir);
    snprintf(elf_f, sizeof(elf_f), "%s/app.elf",     dir);
    snprintf(sig_f, sizeof(sig_f), "%s/app.elf.sig", dir);
    snprintf(man_f, sizeof(man_f), "%s/manifest.ini", dir);
    snprintf(elf_p, sizeof(elf_p), "%s.part", elf_f);
    snprintf(sig_p, sizeof(sig_p), "%s.part", sig_f);
    snprintf(man_p, sizeof(man_p), "%s.part", man_f);

    as_status("Downloading app.elf...", MK_PAL::ACCENT);
    snprintf(url, sizeof(url), "%s%s", AS_BASE, a.elf);
    if (!as_http_get_to_file(url, elf_p)) { as_modal("Download failed", "app.elf", MK_PAL::ERR); return false; }
    snprintf(url, sizeof(url), "%s%s", AS_BASE, a.sig);
    if (!as_http_get_to_file(url, sig_p)) { SD_MMC.remove(elf_p); as_modal("Download failed", "signature", MK_PAL::ERR); return false; }

    /* Verify the detached Ed25519 signature on-device before keeping anything. */
    as_status("Verifying signature...", MK_PAL::ACCENT);
    File ef = SD_MMC.open(elf_p, FILE_READ);
    File sf = SD_MMC.open(sig_p, FILE_READ);
    bool verified = false;
    if (ef && sf && sf.size() == 64) {
        size_t elen = ef.size();
        uint8_t* ebuf = (uint8_t*)heap_caps_malloc(elen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        uint8_t  sig[64];
        if (ebuf && ef.read(ebuf, elen) == (int)elen && sf.read(sig, 64) == 64)
            verified = meow_app_verify(ebuf, elen, sig);
        if (ebuf) heap_caps_free(ebuf);
    }
    if (ef) ef.close();
    if (sf) sf.close();

    if (!verified) {
        SD_MMC.remove(elf_p); SD_MMC.remove(sig_p);
        as_modal("Signature invalid", "App rejected (not trusted)", MK_PAL::ERR);
        return false;
    }

    snprintf(url, sizeof(url), "%s%s", AS_BASE, a.manifest);
    if (!as_http_get_to_file(url, man_p)) { SD_MMC.remove(elf_p); SD_MMC.remove(sig_p); as_modal("Download failed", "manifest", MK_PAL::ERR); return false; }

    /* Commit: replace any existing files with the verified downloads. */
    SD_MMC.remove(elf_f); SD_MMC.remove(sig_f); SD_MMC.remove(man_f);
    bool ok = SD_MMC.rename(elf_p, elf_f) && SD_MMC.rename(sig_p, sig_f) && SD_MMC.rename(man_p, man_f);
    if (!ok) { SD_MMC.remove(elf_p); SD_MMC.remove(sig_p); SD_MMC.remove(man_p);
               as_modal("Install failed", "SD write error", MK_PAL::ERR); return false; }
    return true;
}

/* ── Browse list + main flow. ───────────────────────────────────────────── */
static const char* state_label(int s) { return s == AS_INSTALLED ? "installed" : s == AS_UPDATE ? "UPDATE" : "install"; }

static void as_draw_list(int sel, int top)
{
    LGFX_Class& lcd = s_dev->Lcd;
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "App Store");
    const int rows = 5;
    for (int r = 0; r < rows && top + r < s_napps; ++r) {
        int i = top + r;
        char sub[40];
        snprintf(sub, sizeof(sub), "v%s  %s", s_apps[i].version, state_label(s_apps[i].state));
        MK_TUI::drawMenuItem(lcd, r, s_apps[i].name, sub, i == sel);
    }
    MK_TUI::drawFooter(lcd, "Install", "Exit");
}

void app_store_run(DEVICES* dev)
{
    s_dev = dev;
    if (!as_wifi_connect()) return;

    as_status("Fetching catalog...", MK_PAL::ACCENT);
    const size_t CAP = 16384;
    uint8_t* buf = (uint8_t*)heap_caps_malloc(CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { as_modal("Out of memory", "", MK_PAL::ERR); return; }
    int n = as_http_get(AS_CATALOG, buf, CAP - 1);
    if (n <= 0) { heap_caps_free(buf); as_modal("Catalog fetch failed", "Check WiFi", MK_PAL::ERR); return; }
    buf[n] = 0;
    s_napps = as_parse_catalog((const char*)buf, n);
    heap_caps_free(buf);
    if (s_napps == 0) { as_modal("No apps in catalog", "", MK_PAL::WARN); return; }
    as_mark_states();

    int sel = 0, top = 0;
    const int rows = 5;
    for (;;) {
        as_draw_list(sel, top);
        /* input */
        bool up = false, down = false, a = false, b = false;
        while (!up && !down && !a && !b) {
            s_dev->button.update(); s_dev->button.tick();
            up   = s_dev->button.Up.pressed();
            down = s_dev->button.Down.pressed();
            a    = s_dev->button.A.pressed();
            b    = s_dev->button.B.pressed();
            if (s_dev->button.B.isLongPress()) b = true;
            delay(20);
        }
        if (b) break;
        if (up   && sel > 0)            --sel;
        if (down && sel < s_napps - 1)  ++sel;
        if (sel < top)            top = sel;
        if (sel > top + rows - 1) top = sel - rows + 1;
        if (a) {
            const StoreApp& app = s_apps[sel];
            if (app.state == AS_INSTALLED) {
                if (!as_confirm(app.name, "Already installed — reinstall?", "Yes", "No")) continue;
            } else {
                char l2[48]; snprintf(l2, sizeof(l2), "by %s  v%s", app.author, app.version);
                if (!as_confirm(app.name, l2, "Install", "Back")) continue;
            }
            if (as_install(app)) {
                native_apps_scan();                 /* refresh launcher tiles */
                as_mark_states();
                as_modal("Installed", app.name, MK_PAL::OK);
            }
        }
    }
    native_apps_scan();
}
