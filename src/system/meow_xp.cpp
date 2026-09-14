/**
 * @file  meow_xp.cpp
 * @brief Device-wide XP / leveling (see meow_xp.h).
 *
 * Storage strategy: RAM is the live copy; NVS is written on every earn event
 * (cheap, wear-levelled) so a crash loses nothing; the SD file /system/xp.txt
 * is flushed lazily (~once a minute) as the durable, portable, reflash-proof
 * copy — and it WINS on boot if present.
 */
#include "meow_xp.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <cstring>
#include <cstdlib>
#include "persist.h"

#define XP_DIR   "/system"
#define XP_PATH  "/system/xp.txt"

/* Tamper check: the SD file carries a checksum of its values salted with this
 * constant. An edited file fails to validate and is ignored on load. The salt
 * is fixed (not device-keyed) so the card stays portable between MeowKits; the
 * firmware is open-source, so this only deters casual hand-editing, by design. */
#define XP_SALT  0x4D656F77u   /* "Meow" */

/* FNV-1a over the four counters + salt. */
static uint32_t xp_checksum(uint32_t xp, uint32_t hs, uint32_t play, uint32_t apps)
{
    uint32_t vals[5] = { xp, hs, play, apps, XP_SALT };
    uint32_t h = 2166136261u;
    const uint8_t* p = (const uint8_t*)vals;
    for (size_t i = 0; i < sizeof(vals); i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

/* Cumulative XP to REACH level L: 25*(L-1)*L  →  0,50,150,300,500,750,1050… */
static uint32_t xp_for_level(int L) { return (uint32_t)(25 * (L - 1) * L); }

/* ── Live state ─────────────────────────────────────────────────── */
static uint32_t s_xp    = 0;    /* total lifetime XP                 */
static uint32_t s_hs    = 0;    /* lifetime handshakes               */
static uint32_t s_play  = 0;    /* lifetime awake seconds            */
static uint32_t s_apps  = 0;    /* bitmask: apps opened at least once */
static int      s_level = 1;    /* cached current level              */

static int      s_levelup_new = 0;  /* pending level-up to report (0 = none) */

static uint32_t s_awake_ms = 0; /* sub-minute awake accumulator      */
static uint32_t s_idle_ms  = 0; /* sub-5-minute idle accumulator     */
static uint32_t s_last_ms  = 0; /* millis() at previous tick         */
static uint32_t s_last_open= 0; /* millis() of last app-open award    */
static uint32_t s_sd_dirty_ms = 0;  /* millis() when SD last went dirty (0=clean) */

/* ── Level / title helpers ──────────────────────────────────────── */
int meow_xp_level(void)
{
    int L = 1;
    while (xp_for_level(L + 1) <= s_xp && L < 99) L++;
    return L;
}

uint8_t meow_xp_pct(void)
{
    int L = meow_xp_level();
    uint32_t base = xp_for_level(L), next = xp_for_level(L + 1);
    if (next <= base) return 0;
    return (uint8_t)((uint64_t)(s_xp - base) * 100 / (next - base));
}

const char* meow_xp_title(int level)
{
    if (level <= 1)  return "Kitten";
    if (level == 2)  return "Script Kitty";
    if (level == 3)  return "Packet Prowler";
    if (level == 4)  return "Handshake Hunter";
    if (level == 5)  return "WiFi Wizard";
    if (level <= 7)  return "Deauth Diva";
    return "Legendary Meow";
}

uint32_t    meow_xp_total(void)          { return s_xp; }
uint32_t    meow_xp_stat_handshakes(void){ return s_hs; }
uint32_t    meow_xp_stat_playtime(void)  { return s_play; }

/* ── Persistence ────────────────────────────────────────────────── */
static bool sd_ready(void)
{
    File r = SD_MMC.open("/");
    if (!r) return false;
    r.close();
    return true;
}

static void nvs_save(void)
{
    persist_set_u32(PKEY_XP_TOTAL, s_xp);
    persist_set_u32(PKEY_XP_HS,    s_hs);
    persist_set_u32(PKEY_XP_PLAY,  s_play);
    persist_set_u32(PKEY_XP_APPS,  s_apps);
}

static void nvs_load(void)
{
    s_xp   = persist_get_u32(PKEY_XP_TOTAL, 0);
    s_hs   = persist_get_u32(PKEY_XP_HS,    0);
    s_play = persist_get_u32(PKEY_XP_PLAY,  0);
    s_apps = persist_get_u32(PKEY_XP_APPS,  0);
}

static void sd_write(void)
{
    if (!sd_ready()) return;
    if (!SD_MMC.exists(XP_DIR)) SD_MMC.mkdir(XP_DIR);
    File f = SD_MMC.open(XP_PATH, FILE_WRITE);
    if (!f) return;
    f.printf("# MeowKit XP — device level & lifetime stats (sum guards edits)\n");
    f.printf("xp=%lu\n",   (unsigned long)s_xp);
    f.printf("hs=%lu\n",   (unsigned long)s_hs);
    f.printf("play=%lu\n", (unsigned long)s_play);
    f.printf("apps=%lu\n", (unsigned long)s_apps);
    f.printf("sum=%08lx\n",
             (unsigned long)xp_checksum(s_xp, s_hs, s_play, s_apps));
    f.close();
    s_sd_dirty_ms = 0;
}

/* Parse /system/xp.txt. Commits to RAM only if the checksum validates; a
 * missing or wrong sum means the file was edited (or truncated) — ignore it and
 * keep the NVS copy. Returns true only when values were actually applied. */
static bool sd_read(void)
{
    if (!sd_ready() || !SD_MMC.exists(XP_PATH)) return false;
    File f = SD_MMC.open(XP_PATH, FILE_READ);
    if (!f) return false;

    uint32_t xp = 0, hs = 0, play = 0, apps = 0, sum = 0;
    bool have_sum = false;
    char line[48];
    while (f.available()) {
        int n = f.readBytesUntil('\n', (uint8_t*)line, sizeof(line) - 1);
        if (n <= 0) continue;
        line[n] = '\0';
        if (line[0] == '#') continue;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* val = eq + 1;
        if      (!strcmp(line, "xp"))   xp   = (uint32_t)strtoul(val, nullptr, 10);
        else if (!strcmp(line, "hs"))   hs   = (uint32_t)strtoul(val, nullptr, 10);
        else if (!strcmp(line, "play")) play = (uint32_t)strtoul(val, nullptr, 10);
        else if (!strcmp(line, "apps")) apps = (uint32_t)strtoul(val, nullptr, 10);
        else if (!strcmp(line, "sum"))  { sum = (uint32_t)strtoul(val, nullptr, 16); have_sum = true; }
    }
    f.close();

    if (!have_sum || sum != xp_checksum(xp, hs, play, apps)) {
        Serial.println("[xp] backup checksum mismatch — ignored");
        return false;
    }
    s_xp = xp; s_hs = hs; s_play = play; s_apps = apps;
    return true;
}

bool meow_xp_has_backup(void) { return sd_ready() && SD_MMC.exists(XP_PATH); }

void meow_xp_backup(void)  { sd_write(); }

void meow_xp_restore(void)
{
    if (sd_read()) { s_level = meow_xp_level(); nvs_save(); }
}

void meow_xp_init(void)
{
    nvs_load();          /* fast internal copy first */
    sd_read();           /* SD wins if present: overwrites the fields it holds */
    s_level    = meow_xp_level();
    s_last_ms  = millis();
    s_last_open= 0;
    s_sd_dirty_ms = 0;
    Serial.printf("[xp] level %d (%lu XP), %lu handshakes\n",
                  s_level, (unsigned long)s_xp, (unsigned long)s_hs);
}

void meow_xp_flush(void)
{
    if (s_sd_dirty_ms) sd_write();
}

/* Re-check the level after XP changed; queue a toast if it went up. */
static void check_levelup(void)
{
    int L = meow_xp_level();
    if (L > s_level) s_levelup_new = L;   /* report the newest level reached */
    s_level = L;
}

/* ── Earning ────────────────────────────────────────────────────── */
void meow_xp_add(uint32_t amount)
{
    if (!amount) return;
    s_xp += amount;
    check_levelup();
    nvs_save();
    if (!s_sd_dirty_ms) s_sd_dirty_ms = millis();
}

void meow_xp_add_handshake(uint32_t n)
{
    if (!n) return;
    s_hs += n;
    meow_xp_add(MEOW_XP_HANDSHAKE * n);
}

void meow_xp_app_open(int app_id)
{
    /* First-ever open of this app → discovery bonus (tracked in a 32-bit mask). */
    if (app_id >= 0 && app_id < 32) {
        uint32_t bit = 1u << app_id;
        if (!(s_apps & bit)) {
            s_apps |= bit;
            meow_xp_add(MEOW_XP_APP_FIRST);
            return;
        }
    }
    /* Otherwise a small reward, rate-limited to once per 2 min (no launcher-spam). */
    uint32_t now = millis();
    if (s_last_open && now - s_last_open < 120000) return;
    s_last_open = now;
    meow_xp_add(MEOW_XP_APP_OPEN);
}

void meow_xp_tick(bool awake)
{
    uint32_t now = millis();
    uint32_t dt  = now - s_last_ms;
    s_last_ms = now;
    if (dt > 5000) dt = 5000;    /* clamp: ignore gaps from sleep/app-switching */

    if (awake) {
        s_awake_ms += dt;
        while (s_awake_ms >= 60000) {     /* +1 XP per awake minute */
            s_awake_ms -= 60000;
            s_play += 60;
            meow_xp_add(1);
        }
    } else {
        s_idle_ms += dt;
        while (s_idle_ms >= 300000) {     /* +1 XP per 5 idle minutes */
            s_idle_ms -= 300000;
            meow_xp_add(1);
        }
    }

    /* Lazy SD flush: at most once a minute after the state first goes dirty. */
    if (s_sd_dirty_ms && now - s_sd_dirty_ms >= 60000) sd_write();
}

int meow_xp_poll_levelup(void)
{
    int v = s_levelup_new;
    s_levelup_new = 0;
    return v;
}
