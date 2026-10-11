/**
 * @file  app_sdk.cpp
 * @brief See app_sdk.h. Implements the mk_app ABI and exports it to the ELF
 *        loader so native SD apps can call into the firmware.
 */
#include "app_sdk.h"
#include "mk_nes_abi.h"
#include "emulation/nes_service.h"
#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <LovyanGFX.hpp>
#include "../bsp/devices.h"
#include "../bsp/wifi/WiFi_Class.hpp"
#include "../app/app_common/mk_tui.h"
#include "deauth_monitor.h"
#include "beacon_flood.h"
#include "ble_spam_monitor.h"
#include "probe_monitor.h"
#include "tracker_monitor.h"
#include "tracker_finder.h"
#include "media/audio_service.h"     /* MeowPlayer audio engine */
#include "../bsp/config.h"           /* HAL_IOEXP_PA_EN */
#include "esp_elf.h"                 /* esp_elf_register_symbol / esp_elfsym */
#include "settings_bridge.h"         /* settings_get_sleep_mode / sys_get_brightness */
#include "power_mgmt.h"              /* PEK short press + light sleep */
#include "usb_msc.h"                /* usb_msc_is_active */

/* Active device + lazily-allocated offscreen canvas for the current app run. */
static DEVICES*           s_dev    = nullptr;
static lgfx::LGFX_Sprite* s_canvas = nullptr;
static bool               s_have   = false;

/* Firmware-owned monitor services: promiscuous capture stays in firmware; apps
 * drive them through the mk_deauth_ and mk_flood_ ABI functions. */
static DeauthMonitor  s_deauth;
static BeaconFlood    s_flood;
static BleSpamMonitor s_blespam;
static ProbeMonitor   s_probe;
static TrackerMonitor s_tracker;
static TrackerFinder  s_trkfinder;
static uint32_t       s_trk_sel = 0;   /* selected candidate id (0 = none) */
static meow::media::AudioService s_media;
static bool s_tracker_audio_ready = false;

/* Draw target: the canvas if it allocated, else straight to the LCD. */
static inline void ensure_canvas()
{
    if (s_canvas || !s_dev) return;
    s_canvas = new lgfx::LGFX_Sprite(&s_dev->Lcd);
    if (s_canvas) {
        s_canvas->setColorDepth(16);
        s_canvas->setPsram(true);
        s_have = s_canvas->createSprite(MK_LAYOUT::W, MK_LAYOUT::H);
        if (!s_have) { delete s_canvas; s_canvas = nullptr; }
    }
}

/* ── ABI implementation (extern "C" — names must match the app's calls) ──── */
extern "C" {

void mk_gfx_clear(void)
{
    ensure_canvas();
    if (s_have) MK_TUI::clearScreen(*s_canvas);
    else if (s_dev) MK_TUI::clearScreen(s_dev->Lcd);
}

void mk_gfx_header(const char* title)
{
    if (s_have) MK_TUI::drawHeader(*s_canvas, title);
    else if (s_dev) MK_TUI::drawHeader(s_dev->Lcd, title);
}

void mk_gfx_footer(const char* left, const char* right)
{
    if (s_have) MK_TUI::drawFooter(*s_canvas, left, right);
    else if (s_dev) MK_TUI::drawFooter(s_dev->Lcd, left, right);
}

void mk_gfx_menu_item(int row, const char* name, const char* value, int selected)
{
    if (s_have) MK_TUI::drawMenuItem(*s_canvas, row, name, value, selected != 0);
    else if (s_dev) MK_TUI::drawMenuItem(s_dev->Lcd, row, name, value, selected != 0);
}

static void draw_text(LovyanGFX& g, int x, int y, const char* s, uint32_t color)
{
    g.setFont(&fonts::efontCN_16);
    g.setTextColor(color);          /* transparent bg — draws over banners/graphs */
    g.setCursor(x, y);
    g.print(s);
}

void mk_gfx_text(int x, int y, const char* s, uint32_t color)
{
    if (s_have) draw_text(*s_canvas, x, y, s, color);
    else if (s_dev) draw_text(s_dev->Lcd, x, y, s, color);
}

static void draw_text_sz(LovyanGFX& g, int x, int y, const char* s, uint32_t color, int size)
{
    g.setFont(&fonts::efontCN_16);
    g.setTextColor(color);          /* transparent bg */
    g.setTextSize(size < 1 ? 1 : size);
    g.setCursor(x, y);
    g.print(s);
    g.setTextSize(1);
}

void mk_gfx_text_sz(int x, int y, const char* s, uint32_t color, int size)
{
    if (s_have) draw_text_sz(*s_canvas, x, y, s, color, size);
    else if (s_dev) draw_text_sz(s_dev->Lcd, x, y, s, color, size);
}

void mk_gfx_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    if (s_have) s_canvas->fillRect(x, y, w, h, color);
    else if (s_dev) s_dev->Lcd.fillRect(x, y, w, h, color);
}

void mk_gfx_rect(int x, int y, int w, int h, uint32_t color)
{
    if (s_have) s_canvas->drawRect(x, y, w, h, color);
    else if (s_dev) s_dev->Lcd.drawRect(x, y, w, h, color);
}

void mk_gfx_fill_round_rect(int x, int y, int w, int h, int r, uint32_t color)
{
    if (s_have) s_canvas->fillRoundRect(x, y, w, h, r, color);
    else if (s_dev) s_dev->Lcd.fillRoundRect(x, y, w, h, r, color);
}

void mk_gfx_circle(int x, int y, int r, uint32_t color)
{
    if (s_have) s_canvas->drawCircle(x, y, r, color);
    else if (s_dev) s_dev->Lcd.drawCircle(x, y, r, color);
}

void mk_gfx_fill_circle(int x, int y, int r, uint32_t color)
{
    if (s_have) s_canvas->fillCircle(x, y, r, color);
    else if (s_dev) s_dev->Lcd.fillCircle(x, y, r, color);
}

void mk_gfx_fill_triangle(int x0,int y0,int x1,int y1,int x2,int y2, uint32_t color)
{
    if (s_have) s_canvas->fillTriangle(x0,y0,x1,y1,x2,y2,color);
    else if (s_dev) s_dev->Lcd.fillTriangle(x0,y0,x1,y1,x2,y2,color);
}
void mk_gfx_line(int x0, int y0, int x1, int y1, uint32_t color)
{
    if (s_have) s_canvas->drawLine(x0, y0, x1, y1, color);
    else if (s_dev) s_dev->Lcd.drawLine(x0, y0, x1, y1, color);
}

void mk_gfx_present(void)
{
    if (s_have && s_dev) s_canvas->pushSprite(&s_dev->Lcd, 0, 0);
    /* no canvas → we already drew straight to the LCD */
}

int mk_content_rows(void) { return MK_LAYOUT::CONTENT_ROWS; }

void mk_wifi_begin(void) { if (s_dev) s_dev->wifi.begin(); }

int mk_wifi_scan(mk_ap_t* out, int max)
{
    if (!s_dev || !out || max <= 0) return -1;
    static WiFiAPInfo tmp[48];
    if (max > (int)(sizeof(tmp) / sizeof(tmp[0]))) max = sizeof(tmp) / sizeof(tmp[0]);
    int n = s_dev->wifi.scan(tmp, max);
    if (n < 0) return n;
    for (int i = 0; i < n; i++) {
        memcpy(out[i].ssid, tmp[i].ssid, sizeof(out[i].ssid));
        out[i].ssid[sizeof(out[i].ssid) - 1] = '\0';
        out[i].rssi      = tmp[i].rssi;
        out[i].channel   = tmp[i].channel;
        out[i].encrypted = tmp[i].encrypted ? 1 : 0;
        out[i].auth      = tmp[i].auth;
        memcpy(out[i].bssid, tmp[i].bssid, 6);
    }
    return n;
}

/* ── Deauth monitor service ─────────────────────────────────────────────── */
void mk_deauth_begin(void)  { s_deauth.begin(); }
void mk_deauth_loop(void)   { s_deauth.loop(); }
void mk_deauth_pause(void)  { s_deauth.pause(); }
void mk_deauth_resume(void) { s_deauth.resume(); }
void mk_deauth_stop(void)   { s_deauth.stop(); }
int  mk_deauth_running(void) { return s_deauth.running() ? 1 : 0; }

void mk_deauth_stats(mk_deauth_stats_t* out)
{
    if (!out) return;
    const DeauthStats& s = s_deauth.stats();
    out->total    = s.total;
    out->rate     = s.rate;
    out->peak     = s.peak;
    out->channel  = s.channel;
    out->alert    = s.alert ? 1 : 0;
    out->uptime_s = s_deauth.uptime_s();
    out->running  = s_deauth.running() ? 1 : 0;
}

int mk_deauth_history(uint16_t* out, int max)
{
    if (!out || max <= 0) return 0;
    static uint16_t h[DeauthMonitor::HIST];
    s_deauth.history(h);
    int n = max < DeauthMonitor::HIST ? max : DeauthMonitor::HIST;
    for (int i = 0; i < n; i++) out[i] = h[i];
    return n;
}

int mk_deauth_attackers(mk_attacker_t* out, int max)
{
    if (!out || max <= 0) return 0;
    static AttackerEntry a[16];
    int cap = max < 16 ? max : 16;
    int n = s_deauth.attackers(a, cap);
    for (int i = 0; i < n; i++) {
        memcpy(out[i].mac,    a[i].mac,    6);
        memcpy(out[i].victim, a[i].victim, 6);
        out[i].count   = a[i].count;
        out[i].rssi    = a[i].rssi;
        out[i].channel = a[i].channel;
    }
    return n;
}

/* ── Beacon-flood monitor service ───────────────────────────────────────── */
void mk_flood_begin(void)  { s_flood.begin(); }
void mk_flood_loop(void)   { s_flood.loop(); }
void mk_flood_pause(void)  { s_flood.pause(); }
void mk_flood_resume(void) { s_flood.resume(); }
void mk_flood_stop(void)   { s_flood.stop(); }
int  mk_flood_running(void) { return s_flood.running() ? 1 : 0; }

void mk_flood_stats(mk_flood_stats_t* out)
{
    if (!out) return;
    const FloodStats& s = s_flood.stats();
    out->total     = s.total;
    out->rate      = s.rate;
    out->uniq      = s.uniq;
    out->proberesp = s.proberesp;
    out->peak_uniq = s.peak_uniq;
    out->alert     = s.alert ? 1 : 0;
    out->channel   = s.channel;
    out->uptime_s  = s_flood.uptime_s();
    out->running   = s_flood.running() ? 1 : 0;
}

int mk_flood_history(uint16_t* out, int max)
{
    if (!out || max <= 0) return 0;
    static uint16_t h[BeaconFlood::HIST];
    s_flood.history(h);
    int n = max < BeaconFlood::HIST ? max : BeaconFlood::HIST;
    for (int i = 0; i < n; i++) out[i] = h[i];
    return n;
}

/* ── BLE-spam monitor service ───────────────────────────────────────────── */
void mk_blespam_begin(void)  { s_blespam.begin(); }
void mk_blespam_loop(void)   { s_blespam.loop(); }
void mk_blespam_pause(void)  { s_blespam.pause(); }
void mk_blespam_resume(void) { s_blespam.resume(); }
void mk_blespam_stop(void)   { s_blespam.stop(); }
int  mk_blespam_running(void) { return s_blespam.running() ? 1 : 0; }

void mk_blespam_stats(mk_blespam_stats_t* out)
{
    if (!out) return;
    const BleSpamStats& s = s_blespam.stats();
    out->total    = s.total;
    out->rate     = s.rate;
    out->peak     = s.peak;
    out->alert    = s.alert ? 1 : 0;
    out->apple    = s.apple;
    out->google   = s.google;
    out->ms       = s.ms;
    out->samsung  = s.samsung;
    out->uptime_s = s_blespam.uptime_s();
    out->running  = s_blespam.running() ? 1 : 0;
}

int mk_blespam_history(uint16_t* out, int max)
{
    if (!out || max <= 0) return 0;
    static uint16_t h[BleSpamMonitor::HIST];
    s_blespam.history(h);
    int n = max < BleSpamMonitor::HIST ? max : BleSpamMonitor::HIST;
    for (int i = 0; i < n; i++) out[i] = h[i];
    return n;
}

/* ── Probe-request sniffer service ──────────────────────────────────────── */
void mk_probe_begin(void)  { s_probe.begin(); }
void mk_probe_loop(void)   { s_probe.loop(); }
void mk_probe_pause(void)  { s_probe.pause(); }
void mk_probe_resume(void) { s_probe.resume(); }
void mk_probe_stop(void)   { s_probe.stop(); }
int  mk_probe_running(void) { return s_probe.running() ? 1 : 0; }

void mk_probe_stats(mk_probe_stats_t* out)
{
    if (!out) return;
    const ProbeStats& s = s_probe.stats();
    out->total    = s.total;
    out->rate     = s.rate;
    out->peak     = s.peak;
    out->devices  = s.devices;
    out->channel  = s.channel;
    out->uptime_s = s_probe.uptime_s();
    out->running  = s_probe.running() ? 1 : 0;
}

int mk_probe_history(uint16_t* out, int max)
{
    if (!out || max <= 0) return 0;
    static uint16_t h[ProbeMonitor::HIST];
    s_probe.history(h);
    int n = max < ProbeMonitor::HIST ? max : ProbeMonitor::HIST;
    for (int i = 0; i < n; i++) out[i] = h[i];
    return n;
}

int mk_probe_devices(mk_probe_dev_t* out, int max)
{
    if (!out || max <= 0) return 0;
    static ProbeEntry e[ProbeMonitor::MAXDEV];
    int cap = max < ProbeMonitor::MAXDEV ? max : ProbeMonitor::MAXDEV;
    int n = s_probe.devices(e, cap);
    for (int i = 0; i < n; i++) {
        memcpy(out[i].mac, e[i].mac, 6);
        memcpy(out[i].ssid, e[i].ssid, sizeof(out[i].ssid));
        out[i].ssid[sizeof(out[i].ssid) - 1] = '\0';
        out[i].count   = e[i].count;
        out[i].rssi    = e[i].rssi;
        out[i].channel = e[i].channel;
    }
    return n;
}

/* ── Tracker detector service ───────────────────────────────────────────── */
void mk_tracker_begin(void)
{
    s_trk_sel = 0;
    s_trkfinder.reset();
    s_tracker.begin();
}
void mk_tracker_loop(void)   { s_tracker.loop(); }
void mk_tracker_pause(void)  { s_tracker.pause(); }
void mk_tracker_resume(void) { s_tracker.resume(); }
void mk_tracker_stop(void)
{
    s_tracker.stop(); s_trk_sel = 0; s_trkfinder.reset();
    if (s_tracker_audio_ready && s_dev) {
        s_dev->io_exp.digitalWrite(HAL_IOEXP_PA_EN, LOW);
        s_dev->speaker.end();
        s_tracker_audio_ready = false;
    }
}
int  mk_tracker_running(void)  { return s_tracker.running()  ? 1 : 0; }
int  mk_tracker_starting(void) { return s_tracker.starting() ? 1 : 0; }

int mk_tracker_error(char* out, int max)
{
    const char* e = s_tracker.error();
    if (!e || !e[0]) return 0;
    if (out && max > 0) { strncpy(out, e, max - 1); out[max - 1] = 0; }
    return 1;
}

void mk_tracker_stats(mk_tracker_stats_t* out)
{
    if (!out) return;
    const TrackerStats& s = s_tracker.stats();
    out->nearby     = s.nearby;
    out->persistent = s.persistent;
    out->alert      = s.alert ? 1 : 0;
    out->running    = s_tracker.running() ? 1 : 0;
}

int mk_tracker_list(mk_tracker_t* out, int max)
{
    if (!out || max <= 0) return 0;
    static TrackerEntry e[TrackerMonitor::MAXTRK];
    int cap = max < TrackerMonitor::MAXTRK ? max : TrackerMonitor::MAXTRK;
    int n = s_tracker.trackers(e, cap);
    for (int i = 0; i < n; i++) {
        memcpy(out[i].mac, e[i].mac, 6);
        out[i].type          = e[i].type;
        out[i].rssi          = e[i].rssi;
        out[i].count         = e[i].count;
        out[i].first_ms      = e[i].first_ms;
        out[i].last_ms       = e[i].last_ms;
        out[i].id            = e[i].id;
        out[i].filtered_rssi = e[i].filtered_rssi;
        out[i].scan_fresh    = e[i].scan_fresh ? 1 : 0;
    }
    return n;
}

int mk_tracker_select(uint32_t id)
{
    const bool ok = s_tracker.select(id);
    if (!ok && id) return 0;
    s_trk_sel = id;
    const uint32_t now = millis();
    const bool running = s_tracker.running();
    TrackerEntry e{};
    if (id && s_tracker.tracker(id, e)) s_trkfinder.select(e, now, running);
    else s_trkfinder.reset();
    return 1;
}

void mk_tracker_finder(mk_tracker_finder_t* out)
{
    if (!out) return;
    const uint32_t now = millis();
    const bool running = s_tracker.running();

    TrackerEntry e{};
    const bool have = s_trk_sel && s_tracker.tracker(s_trk_sel, e);
    s_trkfinder.update(have ? &e : nullptr, now, running);

    memset(out, 0, sizeof(*out));
    out->has_target = s_trkfinder.hasTarget() ? 1 : 0;
    switch (s_trkfinder.state(now, running)) {
        case TrackerFinder::State::Live:    out->state = MK_TRK_LIVE;    break;
        case TrackerFinder::State::Waiting: out->state = MK_TRK_WAITING; break;
        case TrackerFinder::State::Lost:    out->state = MK_TRK_LOST;    break;
        default:                            out->state = MK_TRK_PAUSED;  break;
    }
    out->strength = (int16_t)s_trkfinder.strength();
    int dir = 0;
    out->trend_ready = s_trkfinder.trend(now, running, dir) ? 1 : 0;
    out->trend       = (int8_t)dir;

    const TrackerEntry& t = s_trkfinder.target();
    out->filtered_rssi = t.filtered_rssi;
    out->age_ms        = s_trkfinder.hasTarget() ? s_trkfinder.age(now) : 0;
    out->target_id     = t.id;
    memcpy(out->mac, t.mac, 6);
    out->type          = t.type;
    out->scan_fresh    = t.scan_fresh ? 1 : 0;

    unsigned hc = s_trkfinder.historyCount();
    if (hc > MK_TRK_HISTORY) hc = MK_TRK_HISTORY;
    out->history_count = (uint16_t)hc;
    for (unsigned i = 0; i < hc; i++) out->history[i] = s_trkfinder.history(i);
}

/* ── Media / audio player service ───────────────────────────────────────── */
int mk_media_begin(void)
{
    if (!s_dev) return 0;
    s_tracker_audio_ready = false;
    bool ok = s_media.begin(s_dev);
    if (ok && !s_dev->io_exp.digitalWrite(HAL_IOEXP_PA_EN, HIGH)) {
        s_media.end();
        ok = false;
    }
    return ok ? 1 : 0;
}

void mk_media_end(void)
{
    s_media.end();
    if (s_dev) s_dev->io_exp.digitalWrite(HAL_IOEXP_PA_EN, LOW);
}

void mk_media_cmd(int action, int value)
{
    s_media.command(meow::media::Command{ (meow::media::Action)action, (int32_t)value });
}

void mk_media_status(mk_media_status_t* out)
{
    if (!out) return;
    meow::media::Status s;
    if (!s_media.status(s)) { memset(out, 0, sizeof(*out)); return; }
    memcpy(out->state, s.state, sizeof(out->state));
    memcpy(out->title, s.title, sizeof(out->title));
    out->position   = s.position;
    out->duration   = s.duration;
    out->generation = s.generation;
    out->tracks     = s.tracks;
    out->volume     = s.volume;
    out->finished   = s.finished;
}

int mk_media_tracks(int offset, mk_track_t* out, int max)
{
    if (!out || max <= 0) return 0;
    int lim = max < (int)meow::media::MaxPage ? max : (int)meow::media::MaxPage;
    meow::media::Page pg{};
    if (!s_media.tracks(0, (size_t)offset, (size_t)lim, pg)) return 0;
    for (size_t i = 0; i < pg.count; i++) {
        out[i].id = pg.entries[i].id;
        memcpy(out[i].title, pg.entries[i].title, sizeof(out[i].title));
        out[i].title[sizeof(out[i].title) - 1] = '\0';
    }
    return (int)pg.count;
}

void mk_media_set_output(int speaker)
{
    if (s_dev) s_dev->io_exp.digitalWrite(HAL_IOEXP_PA_EN, speaker ? HIGH : LOW);
}

int mk_tracker_beep(uint16_t duration_ms)
{
    if (!s_dev || !duration_ms || duration_ms > 120 || s_media.ready()) return 0;
    if (!s_tracker_audio_ready) {
        s_dev->speaker.config().sample_rate = 44100;
        s_dev->speaker.config().bits_per_sample = 16;
        if (!s_dev->speaker.begin(&In_I2C) ||
            !s_dev->speaker.setMute(false)) {
            s_dev->speaker.end();
            return 0;
        }
        if (!s_dev->io_exp.digitalWrite(HAL_IOEXP_PA_EN, HIGH)) {
            s_dev->speaker.end();
            return 0;
        }
        delay(1);
        s_tracker_audio_ready = true;
    }
    s_dev->speaker.tone(900, duration_ms, 18);
    return 1;
}

void mk_input_poll(void)
{
    if (!s_dev) return;
    app_sleep_check();          /* power-button → sleep, works inside any app too */
    s_dev->button.update();
    s_dev->button.tick();
}

static Button_Class* btn_of(int id)
{
    if (!s_dev) return nullptr;
    switch (id) {
        case MK_BTN_A:     return &s_dev->button.A;
        case MK_BTN_B:     return &s_dev->button.B;
        case MK_BTN_UP:    return &s_dev->button.Up;
        case MK_BTN_DOWN:  return &s_dev->button.Down;
        case MK_BTN_LEFT:  return &s_dev->button.Left;
        case MK_BTN_RIGHT: return &s_dev->button.Right;
        default:           return nullptr;
    }
}

int mk_btn(int id)      { Button_Class* b = btn_of(id); return b && b->pressed()     ? 1 : 0; }
int mk_btn_long(int id) { Button_Class* b = btn_of(id); return b && b->isLongPress() ? 1 : 0; }

void     mk_delay(uint32_t ms) { delay(ms); }
uint32_t mk_millis(void)       { return millis(); }

int mk_snprintf(char* buf, size_t n, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

} /* extern "C" */

/* ── Exported symbol table (name → function) ────────────────────────────── */
static const struct esp_elfsym MK_SDK_SYMS[] = {
    { "mk_gfx_clear",     (const void*)&mk_gfx_clear },
    { "mk_gfx_header",    (const void*)&mk_gfx_header },
    { "mk_gfx_footer",    (const void*)&mk_gfx_footer },
    { "mk_gfx_menu_item", (const void*)&mk_gfx_menu_item },
    { "mk_gfx_text",      (const void*)&mk_gfx_text },
    { "mk_gfx_text_sz",   (const void*)&mk_gfx_text_sz },
    { "mk_gfx_fill_rect", (const void*)&mk_gfx_fill_rect },
    { "mk_gfx_rect",      (const void*)&mk_gfx_rect },
    { "mk_gfx_fill_round_rect", (const void*)&mk_gfx_fill_round_rect },
    { "mk_gfx_circle",        (const void*)&mk_gfx_circle },
    { "mk_gfx_fill_circle",   (const void*)&mk_gfx_fill_circle },
    { "mk_gfx_fill_triangle", (const void*)&mk_gfx_fill_triangle },
    { "mk_gfx_line",          (const void*)&mk_gfx_line },
    { "mk_gfx_present",   (const void*)&mk_gfx_present },
    { "mk_content_rows",  (const void*)&mk_content_rows },
    { "mk_wifi_begin",    (const void*)&mk_wifi_begin },
    { "mk_wifi_scan",     (const void*)&mk_wifi_scan },
    { "mk_deauth_begin",     (const void*)&mk_deauth_begin },
    { "mk_deauth_loop",      (const void*)&mk_deauth_loop },
    { "mk_deauth_pause",     (const void*)&mk_deauth_pause },
    { "mk_deauth_resume",    (const void*)&mk_deauth_resume },
    { "mk_deauth_stop",      (const void*)&mk_deauth_stop },
    { "mk_deauth_running",   (const void*)&mk_deauth_running },
    { "mk_deauth_stats",     (const void*)&mk_deauth_stats },
    { "mk_deauth_history",   (const void*)&mk_deauth_history },
    { "mk_deauth_attackers", (const void*)&mk_deauth_attackers },
    { "mk_flood_begin",   (const void*)&mk_flood_begin },
    { "mk_flood_loop",    (const void*)&mk_flood_loop },
    { "mk_flood_pause",   (const void*)&mk_flood_pause },
    { "mk_flood_resume",  (const void*)&mk_flood_resume },
    { "mk_flood_stop",    (const void*)&mk_flood_stop },
    { "mk_flood_running", (const void*)&mk_flood_running },
    { "mk_flood_stats",   (const void*)&mk_flood_stats },
    { "mk_flood_history", (const void*)&mk_flood_history },
    { "mk_blespam_begin",   (const void*)&mk_blespam_begin },
    { "mk_blespam_loop",    (const void*)&mk_blespam_loop },
    { "mk_blespam_pause",   (const void*)&mk_blespam_pause },
    { "mk_blespam_resume",  (const void*)&mk_blespam_resume },
    { "mk_blespam_stop",    (const void*)&mk_blespam_stop },
    { "mk_blespam_running", (const void*)&mk_blespam_running },
    { "mk_blespam_stats",   (const void*)&mk_blespam_stats },
    { "mk_blespam_history", (const void*)&mk_blespam_history },
    { "mk_probe_begin",   (const void*)&mk_probe_begin },
    { "mk_probe_loop",    (const void*)&mk_probe_loop },
    { "mk_probe_pause",   (const void*)&mk_probe_pause },
    { "mk_probe_resume",  (const void*)&mk_probe_resume },
    { "mk_probe_stop",    (const void*)&mk_probe_stop },
    { "mk_probe_running", (const void*)&mk_probe_running },
    { "mk_probe_stats",   (const void*)&mk_probe_stats },
    { "mk_probe_history", (const void*)&mk_probe_history },
    { "mk_probe_devices", (const void*)&mk_probe_devices },
    { "mk_tracker_begin",   (const void*)&mk_tracker_begin },
    { "mk_tracker_loop",    (const void*)&mk_tracker_loop },
    { "mk_tracker_pause",   (const void*)&mk_tracker_pause },
    { "mk_tracker_resume",  (const void*)&mk_tracker_resume },
    { "mk_tracker_stop",    (const void*)&mk_tracker_stop },
    { "mk_tracker_running", (const void*)&mk_tracker_running },
    { "mk_tracker_starting",(const void*)&mk_tracker_starting },
    { "mk_tracker_error",   (const void*)&mk_tracker_error },
    { "mk_tracker_stats",   (const void*)&mk_tracker_stats },
    { "mk_tracker_list",    (const void*)&mk_tracker_list },
    { "mk_tracker_select",  (const void*)&mk_tracker_select },
    { "mk_tracker_finder",  (const void*)&mk_tracker_finder },
    { "mk_media_begin",      (const void*)&mk_media_begin },
    { "mk_media_end",        (const void*)&mk_media_end },
    { "mk_media_cmd",        (const void*)&mk_media_cmd },
    { "mk_media_status",     (const void*)&mk_media_status },
    { "mk_media_tracks",     (const void*)&mk_media_tracks },
    { "mk_media_set_output", (const void*)&mk_media_set_output },
    { "mk_tracker_beep", (const void*)&mk_tracker_beep },
    { "mk_input_poll",    (const void*)&mk_input_poll },
    { "mk_btn",           (const void*)&mk_btn },
    { "mk_btn_long",      (const void*)&mk_btn_long },
    { "mk_delay",         (const void*)&mk_delay },
    { "mk_millis",        (const void*)&mk_millis },
    { "mk_snprintf",      (const void*)&mk_snprintf },
    { "mk_nes_version", (const void*)&mk_nes_version },
    { "mk_nes_begin", (const void*)&mk_nes_begin },
    { "mk_nes_end", (const void*)&mk_nes_end },
    { "mk_nes_catalog_open", (const void*)&mk_nes_catalog_open },
    { "mk_nes_catalog_info", (const void*)&mk_nes_catalog_info },
    { "mk_nes_catalog_entry", (const void*)&mk_nes_catalog_entry },
    { "mk_nes_open", (const void*)&mk_nes_open },
    { "mk_nes_run", (const void*)&mk_nes_run },
    { "mk_nes_command", (const void*)&mk_nes_command },
    { "mk_nes_state", (const void*)&mk_nes_state },
    { "mk_nes_error", (const void*)&mk_nes_error },
    { "mk_nes_poll", (const void*)&mk_nes_poll },
    { "mk_nes_wait_release", (const void*)&mk_nes_wait_release },
    /* libc helpers the compiler may emit implicitly (e.g. struct copies). */
    { "memcpy",           (const void*)&memcpy },
    { "memset",           (const void*)&memset },
    { NULL, NULL }   /* ESP_ELFSYM_END */
};

void app_sdk_init(DEVICES* dev)
{
    s_dev = dev;
    nes_service_attach(dev);
    static bool registered = false;
    if (!registered) {
        if (esp_elf_register_symbol(MK_SDK_SYMS) == 0) registered = true;
    }
}

/* Power-button → sleep, callable from any context (launcher home, launcher
 * app-state, or inside an ELF app via mk_input_poll). Self-rate-limited so the
 * AXP173 PEK latch is read at most ~5 Hz regardless of caller. Returns true if
 * it slept (screen was off and restored on wake). */
bool app_sleep_check(void)
{
    if (!s_dev) return false;
    static uint32_t last = 0;
    uint32_t now = millis();
    if (now - last < 200) return false;
    last = now;

    if (!settings_get_sleep_mode())  return false;
    if (!power_consume_pek_short())  return false;
    if (usb_msc_is_active())         return false;   /* don't sleep mid file-transfer */

    uint8_t restore = (uint8_t)(sys_get_brightness() * 255 / 100);
    s_dev->Lcd.setBrightness(0);
    /* Explicit user request: sleep until a button, even on charger (wake_on_charge=false). */
    int r = power_light_sleep(60, POWER_SLEEP_MIN_PCT, false);
    if (r == PWR_WAKE_LOWBAT) power_shutdown();       /* does not return */
    s_dev->Lcd.setBrightness(restore);
    return true;
}
