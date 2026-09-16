/**
 * @file  mk_app_abi.h
 * @brief MeowKit native-app ABI — the stable C interface between the firmware
 *        and signed native ELF apps loaded from the SD card.
 *
 * This is the ONLY surface a native app may call. It is a flat C ABI on purpose:
 * apps are plain C, compiled standalone and linked against nothing — every
 * function here is resolved at load time against the firmware's exported symbol
 * table (see app_sdk.cpp). Apps must NOT pull in LVGL / LovyanGFX / the drivers;
 * they call these wrappers instead.
 *
 * Shared verbatim by the firmware (app_sdk.cpp implements it) and by each app
 * (build_app.sh puts this dir on the include path). Keep it pure C, no deps.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Colors (0xRRGGBB) ─────────────────────────────────────────────────── */
#define MK_COL_BLACK   0x000000u
#define MK_COL_WHITE   0xFFFFFFu
#define MK_COL_ACCENT  0xFF2A3Du   /* FeralCat red */
#define MK_COL_TEXT    0xFFFFFFu
#define MK_COL_MUTED   0x888888u
#define MK_COL_OK      0x00DD44u
#define MK_COL_WARN    0xFFAA00u
#define MK_COL_ERR     0xFF3333u
#define MK_COL_ACCENT_DARK 0x2E0009u
#define MK_COL_ACCENT_DIM  0x8A0016u   /* dim red — inactive radar rings */
#define MK_COL_ITEM_BG     0x1A1A1Au
#define MK_COL_BORDER      0x333333u

/* ── Buttons ───────────────────────────────────────────────────────────── */
enum {
    MK_BTN_A = 0, MK_BTN_B,
    MK_BTN_UP, MK_BTN_DOWN, MK_BTN_LEFT, MK_BTN_RIGHT
};

/* ── WiFi access-point record ──────────────────────────────────────────── */
typedef struct {
    char    ssid[33];
    int8_t  rssi;
    uint8_t channel;
    uint8_t encrypted;   /* 0/1 */
    uint8_t auth;        /* 0=Open 1=WEP 2=WPA 3=WPA2 4=WPA/2 5=EAP 6=WPA3 7=WPA2/3 */
    uint8_t bssid[6];
} mk_ap_t;

/* ── Layout (matches the built-in UI, 320x240) ─────────────────────────── */
#define MK_SCREEN_W    320
#define MK_SCREEN_H    240
#define MK_HDR_H       24
#define MK_ITEM_H      32
#define MK_CONTENT_Y   (MK_HDR_H + 1)
#define MK_PAD         8

/* ── Display ───────────────────────────────────────────────────────────────
 * The firmware owns an offscreen canvas. Draw into it, then present() blits it
 * to the LCD in one shot. */
void mk_gfx_clear(void);
void mk_gfx_header(const char* title);
void mk_gfx_footer(const char* left, const char* right);
void mk_gfx_menu_item(int row, const char* name, const char* value, int selected);
void mk_gfx_text(int x, int y, const char* s, uint32_t color);
void mk_gfx_text_sz(int x, int y, const char* s, uint32_t color, int size);  /* size 1|2 */
void mk_gfx_fill_rect(int x, int y, int w, int h, uint32_t color);
void mk_gfx_rect(int x, int y, int w, int h, uint32_t color);               /* outline */
void mk_gfx_fill_round_rect(int x, int y, int w, int h, int r, uint32_t color);
void mk_gfx_circle(int x, int y, int r, uint32_t color);                  /* outline */
void mk_gfx_fill_circle(int x, int y, int r, uint32_t color);
void mk_gfx_fill_triangle(int x0,int y0,int x1,int y1,int x2,int y2, uint32_t color);
void mk_gfx_line(int x0, int y0, int x1, int y1, uint32_t color);
void mk_gfx_present(void);
int  mk_content_rows(void);   /* number of visible menu rows */

/* ── WiFi ──────────────────────────────────────────────────────────────── */
void mk_wifi_begin(void);
int  mk_wifi_scan(mk_ap_t* out, int max);   /* count, or <0 on error */

/* ── Deauth monitor (passive 802.11 deauth/disassoc detector) ──────────────
 * A firmware-owned service (promiscuous capture stays in firmware). The app
 * drives it (begin/loop/pause…) and polls stats/history/attackers. */
#define MK_DEAUTH_HIST       30
#define MK_DEAUTH_THRESHOLD  6

typedef struct {
    uint32_t total;
    uint16_t rate;
    uint16_t peak;
    uint8_t  channel;
    uint8_t  alert;      /* 0/1 */
    uint32_t uptime_s;
    uint8_t  running;    /* 0/1 */
} mk_deauth_stats_t;

typedef struct {
    uint8_t  mac[6];     /* frame source (the "attacker") */
    uint8_t  victim[6];
    uint16_t count;
    int8_t   rssi;
    uint8_t  channel;
} mk_attacker_t;

void mk_deauth_begin(void);
void mk_deauth_loop(void);
void mk_deauth_pause(void);
void mk_deauth_resume(void);
void mk_deauth_stop(void);
int  mk_deauth_running(void);
void mk_deauth_stats(mk_deauth_stats_t* out);
int  mk_deauth_history(uint16_t* out, int max);        /* up to MK_DEAUTH_HIST */
int  mk_deauth_attackers(mk_attacker_t* out, int max);

/* ── Beacon-flood monitor (Karma / rogue-AP flood detector) ────────────────── */
#define MK_FLOOD_HIST        30
#define MK_FLOOD_ALERT_UNIQ  25

typedef struct {
    uint32_t total;
    uint16_t rate;
    uint16_t uniq;
    uint16_t proberesp;
    uint16_t peak_uniq;
    uint8_t  alert;      /* 0/1 */
    uint8_t  channel;
    uint32_t uptime_s;
    uint8_t  running;    /* 0/1 */
} mk_flood_stats_t;

void mk_flood_begin(void);
void mk_flood_loop(void);
void mk_flood_pause(void);
void mk_flood_resume(void);
void mk_flood_stop(void);
int  mk_flood_running(void);
void mk_flood_stats(mk_flood_stats_t* out);
int  mk_flood_history(uint16_t* out, int max);         /* up to MK_FLOOD_HIST */

/* ── BLE-spam monitor (Apple/Google/MS/Samsung advert flood detector) ──────── */
#define MK_BLESPAM_HIST       30
#define MK_BLESPAM_THRESHOLD  12

typedef struct {
    uint32_t total;
    uint16_t rate;
    uint16_t peak;
    uint8_t  alert;      /* 0/1 */
    uint16_t apple;
    uint16_t google;
    uint16_t ms;
    uint16_t samsung;
    uint32_t uptime_s;
    uint8_t  running;    /* 0/1 */
} mk_blespam_stats_t;

void mk_blespam_begin(void);
void mk_blespam_loop(void);
void mk_blespam_pause(void);
void mk_blespam_resume(void);
void mk_blespam_stop(void);
int  mk_blespam_running(void);
void mk_blespam_stats(mk_blespam_stats_t* out);
int  mk_blespam_history(uint16_t* out, int max);       /* up to MK_BLESPAM_HIST */

/* ── Probe-request sniffer (passive 802.11 probe monitor) ──────────────────── */
#define MK_PROBE_HIST    30
#define MK_PROBE_MAXDEV  24

typedef struct {
    uint32_t total;
    uint16_t rate;
    uint16_t peak;
    uint16_t devices;
    uint8_t  channel;
    uint32_t uptime_s;
    uint8_t  running;    /* 0/1 */
} mk_probe_stats_t;

typedef struct {
    uint8_t  mac[6];
    char     ssid[33];
    uint16_t count;
    int8_t   rssi;
    uint8_t  channel;
} mk_probe_dev_t;

void mk_probe_begin(void);
void mk_probe_loop(void);
void mk_probe_pause(void);
void mk_probe_resume(void);
void mk_probe_stop(void);
int  mk_probe_running(void);
void mk_probe_stats(mk_probe_stats_t* out);
int  mk_probe_history(uint16_t* out, int max);         /* up to MK_PROBE_HIST */
int  mk_probe_devices(mk_probe_dev_t* out, int max);   /* up to MK_PROBE_MAXDEV */

/* ── Unwanted-tracker detector (AirTag / Tile / SmartTag) ──────────────────── */
#define MK_TRK_MAXTRK     24
#define MK_TRK_PERSIST_MS 60000
#define MK_TRK_RECENT_MS  30000
enum { MK_TRK_APPLE = 0, MK_TRK_TILE = 1, MK_TRK_SAMSUNG = 2 };

typedef struct {
    uint16_t nearby;
    uint16_t persistent;
    uint8_t  alert;      /* 0/1 */
    uint8_t  running;    /* 0/1 */
} mk_tracker_stats_t;

typedef struct {
    uint8_t  mac[6];
    uint8_t  type;       /* MK_TRK_* */
    int8_t   rssi;
    uint16_t count;
    uint32_t first_ms;
    uint32_t last_ms;
    uint32_t id;             /* stable, nonzero within this scan session */
    int16_t  filtered_rssi;  /* median + EWMA, in dBm */
    uint8_t  scan_fresh;     /* 1 = valid observation since last pause */
} mk_tracker_t;

/* Signal-finder state for a selected candidate (see mk_tracker_finder). */
enum { MK_TRK_LIVE = 0, MK_TRK_WAITING, MK_TRK_LOST, MK_TRK_PAUSED };
#define MK_TRK_HISTORY 48
typedef struct {
    uint8_t  has_target;     /* 0/1 */
    uint8_t  state;          /* MK_TRK_LIVE / WAITING / LOST / PAUSED */
    uint8_t  trend_ready;    /* 0/1 — enough samples for a trend */
    int8_t   trend;          /* -1 weaker · 0 steady · +1 stronger */
    int16_t  strength;       /* 0..100 relative received strength (not range) */
    int16_t  filtered_rssi;  /* dBm */
    uint32_t age_ms;         /* since last reception of the target */
    uint32_t target_id;
    uint8_t  mac[6];
    uint8_t  type;           /* MK_TRK_* */
    uint8_t  scan_fresh;     /* 0/1 */
    uint16_t history_count;  /* 0..MK_TRK_HISTORY */
    int16_t  history[MK_TRK_HISTORY];  /* oldest→newest filtered RSSI */
} mk_tracker_finder_t;

void mk_tracker_begin(void);
void mk_tracker_loop(void);
void mk_tracker_pause(void);
void mk_tracker_resume(void);
void mk_tracker_stop(void);
int  mk_tracker_running(void);
int  mk_tracker_starting(void);                        /* 1 while BLE scan spins up */
int  mk_tracker_error(char* out, int max);             /* 1 + copies reason if failed */
void mk_tracker_stats(mk_tracker_stats_t* out);
int  mk_tracker_list(mk_tracker_t* out, int max);      /* up to MK_TRK_MAXTRK */
int  mk_tracker_select(uint32_t id);                   /* 0 releases; 1 if present */
void mk_tracker_finder(mk_tracker_finder_t* out);      /* finder state for selection */

/* ── Media / audio player (MeowPlayer) ─────────────────────────────────────
 * The audio engine (ES8311 DAC + Helix MP3 decoder + I2S on a core-0 worker)
 * stays in firmware; the app drives it with commands and polls a status
 * snapshot. Files are scanned from /music (.mp3). */
enum { MK_MEDIA_SCAN = 0, MK_MEDIA_PLAY, MK_MEDIA_PAUSE, MK_MEDIA_STOP,
       MK_MEDIA_VOLUME, MK_MEDIA_SEEK, MK_MEDIA_EQ };

typedef struct {
    char     state[16];   /* idle/scanning/loading/playing/paused/stopped/ended/error */
    char     title[96];
    uint32_t position;    /* seconds */
    uint32_t duration;    /* seconds (approx for MP3) */
    uint32_t generation;  /* bumps when a catalog scan completes */
    uint16_t tracks;
    uint8_t  volume;      /* 0..100 */
    uint32_t finished;    /* increments once on natural end-of-track */
} mk_media_status_t;

typedef struct {
    uint16_t id;          /* stable track id for MK_MEDIA_PLAY value */
    char     title[96];
} mk_track_t;

int  mk_media_begin(void);                 /* start engine; 1 = ok. Sets speaker on. */
void mk_media_end(void);                   /* stop engine + amp */
void mk_media_cmd(int action, int value);  /* MK_MEDIA_*; value = track id / volume / seek sec */
void mk_media_status(mk_media_status_t* out);
int  mk_media_tracks(int offset, mk_track_t* out, int max);  /* fills up to min(max,8); returns count */
void mk_media_set_output(int speaker);     /* 1 = speaker, 0 = headphone jack */

/* ── Input ─────────────────────────────────────────────────────────────── */
void mk_input_poll(void);       /* call once per loop before reading buttons */
int  mk_btn(int id);            /* 1 once per press (edge-triggered) */
int  mk_btn_long(int id);       /* 1 while the button is held long */

/* ── Misc ──────────────────────────────────────────────────────────────── */
void     mk_delay(uint32_t ms);
uint32_t mk_millis(void);
int      mk_snprintf(char* buf, size_t n, const char* fmt, ...);

#ifdef __cplusplus
}
#endif
