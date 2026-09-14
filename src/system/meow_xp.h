/**
 * @file  meow_xp.h
 * @brief MeowKit XP / leveling — a DEVICE-WIDE feature, not tied to any app.
 *
 * The whole point is to reward fidgeting with the device: XP trickles in just
 * from using it, and "finds" (handshakes, detector hits) give a bonus on top.
 *
 * Earning (call these from anywhere):
 *   - meow_xp_tick(awake)   time trickle: +1 XP per awake minute, +1 per 5 idle
 *   - meow_xp_app_open(id)  +2 opening an app (rate-limited), +20 first ever
 *   - meow_xp_add(n)        generic — finds use the MEOW_XP_* amounts below
 *
 * Persistence (survives reboots AND reflashes):
 *   - RAM holds the live counters (earning is free, no I/O)
 *   - NVS is written on each earn event so a crash loses nothing
 *   - SD /system/xp.txt is flushed lazily (~once a minute) as the durable,
 *     portable, reflash-proof copy. On boot the SD copy WINS if present.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* XP awards. Trickle is the driver; finds season it (see the .cpp for rates). */
#define MEOW_XP_HANDSHAKE   15   /* WPA handshake captured (the trophy)        */
#define MEOW_XP_FIND         3   /* detector hit: rogue AP / tracker / spam…   */
#define MEOW_XP_SCRIPT       5   /* ran a Berry script (once per run)          */
#define MEOW_XP_APP_OPEN     2   /* opened an app (rate-limited)               */
#define MEOW_XP_APP_FIRST   20   /* first-ever open of a given app             */

/* ── Lifecycle ─────────────────────────────────────────────────── */
void meow_xp_init(void);        /* load (SD wins, else NVS). Call once at boot. */
void meow_xp_tick(bool awake);  /* every frame: time trickle + lazy SD flush.  */
void meow_xp_flush(void);       /* force an SD sync now (clean shutdown).       */

/* ── Earning ───────────────────────────────────────────────────── */
void meow_xp_add(uint32_t amount);        /* generic (finds, scripts)          */
void meow_xp_add_handshake(uint32_t n);   /* +15 each, tracks lifetime count   */
void meow_xp_app_open(int app_id);        /* +2, or +20 the first time ever    */

/* ── Query ─────────────────────────────────────────────────────── */
uint32_t    meow_xp_total(void);
int         meow_xp_level(void);          /* 1..                               */
uint8_t     meow_xp_pct(void);            /* 0..100 progress to next level      */
const char* meow_xp_title(int level);

/* Level-up notification for the launcher toast: returns the new level ONCE
 * (then clears) after a level-up, else 0. */
int meow_xp_poll_levelup(void);

/* Lifetime stats (for the Stats page). */
uint32_t meow_xp_stat_handshakes(void);
uint32_t meow_xp_stat_playtime(void);     /* seconds spent awake with the device */

/* ── Backup / restore (Settings ▸ Backup) ──────────────────────── */
void meow_xp_backup(void);       /* force-write SD copy now                     */
bool meow_xp_has_backup(void);   /* an SD copy exists (enables Restore)         */
void meow_xp_restore(void);      /* load SD copy into RAM + NVS                 */

#ifdef __cplusplus
}
#endif
