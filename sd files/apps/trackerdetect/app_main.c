/*
 * Tracker Detector — native MeowKit app (signed ELF, app SDK).
 *
 * Passively scans BLE for item-tracker candidates (Apple Find My / AirTag,
 * Tile, Samsung SmartTag). The firmware tracker service does the radio work,
 * median+EWMA filtering, stable per-session ids and candidate selection; this
 * app renders the list and a proximity radar for a chosen candidate.
 *
 * Addresses rotate, so a row is an observation identity within one scan
 * session, not proof of a physical device or of being followed. The radar
 * shows relative signal strength only — never a bearing or a distance.
 *
 * Original feature by Caliun (PR #3); re-ported onto the ELF app SDK.
 *
 * Controls (list):  Up/Down select · A Find · Left/Right pause · hold B exit
 *        (radar):   A pause/resume · tap B list · hold B exit
 */
#include "mk_app_abi.h"

enum { LIST = 0, RADAR };

static mk_tracker_t g_trk[MK_TRK_MAXTRK];
static int          g_n     = 0;
static int          g_sel   = 0;      /* row index in g_trk           */
static uint32_t     g_selid = 0;      /* selected id (stable focus)   */
static int          g_first = 0;      /* first visible list row       */

static const char* type_name(unsigned t)
{
    switch (t) {
        case MK_TRK_APPLE:   return "Find My candidate";
        case MK_TRK_TILE:    return "Tile candidate";
        case MK_TRK_SAMSUNG: return "Samsung candidate";
        default:             return "Tracker candidate";
    }
}

static int strength_for(int rssi)
{
    int v = (rssi + 95) * 100 / 60;
    return v < 0 ? 0 : v > 100 ? 100 : v;
}

/* Keep the cursor on the same identity while RSSI order shifts. */
static void reconcile(void)
{
    if (g_n <= 0) { g_sel = 0; g_selid = 0; g_first = 0; return; }
    if (g_selid) {
        for (int i = 0; i < g_n; i++)
            if (g_trk[i].id == g_selid) { g_sel = i; goto clamp; }
    }
    if (g_sel >= g_n) g_sel = g_n - 1;
    if (g_sel < 0) g_sel = 0;
    g_selid = g_trk[g_sel].id;
clamp:
    if (g_first > g_sel || g_first + 4 <= g_sel) g_first = (g_sel / 4) * 4;
    if (g_first < 0) g_first = 0;
}

/* ── List view ──────────────────────────────────────────────────────────── */
static void draw_list(void)
{
    mk_tracker_stats_t s; mk_tracker_stats(&s);
    char err[40]; int has_err = mk_tracker_error(err, sizeof(err));
    int starting = mk_tracker_starting();
    uint32_t now = mk_millis();

    mk_gfx_clear();
    mk_gfx_header("Tracker Detect");

    /* status pill */
    const char* status = has_err ? "SCAN ERROR" : starting ? "STARTING" :
        !s.running ? "PAUSED" : s.persistent ? "PERSISTENT" : s.nearby ? "NEARBY" : "SCANNING";
    uint32_t scol = has_err ? MK_COL_ERR : starting ? MK_COL_WARN :
        !s.running ? MK_COL_MUTED : s.persistent ? MK_COL_WARN : MK_COL_ACCENT;
    mk_gfx_fill_round_rect(8, 30, 304, 26, 5, MK_COL_ITEM_BG);
    mk_gfx_text(16, 36, status, scol);
    char cnt[24]; mk_snprintf(cnt, sizeof(cnt), "%u/%u", s.nearby, g_n);
    mk_gfx_text(258, 36, cnt, MK_COL_MUTED);
    char np[28]; mk_snprintf(np, sizeof(np), "%u near / %u pers", s.nearby, s.persistent);
    mk_gfx_text(150, 36, np, MK_COL_MUTED);

    if (g_n == 0) {
        mk_gfx_text(MK_PAD, 92,  has_err ? "Scan unavailable." : "No trackers detected.", MK_COL_TEXT);
        mk_gfx_text(MK_PAD, 116, "AirTag / Find My, Tile,", MK_COL_MUTED);
        mk_gfx_text(MK_PAD, 134, "and SmartTag appear here.", MK_COL_MUTED);
        mk_gfx_text(MK_PAD, 166, (s.running || starting) ? "Listening for BLE..."
                                                         : "Left/Right to resume.", MK_COL_MUTED);
        if (has_err) mk_gfx_text(MK_PAD, 190, err, MK_COL_ERR);
        mk_gfx_footer("Find", "Exit");
        mk_gfx_present();
        return;
    }

    char buf[48];
    for (int slot = 0; slot < 4 && g_first + slot < g_n; slot++) {
        int i = g_first + slot;
        mk_tracker_t* e = &g_trk[i];
        int y = 62 + slot * 35;
        int active = (i == g_sel);
        int fresh = s.running && !has_err && !starting && e->scan_fresh &&
                    (uint32_t)(now - e->last_ms) <= 2500;
        int persistent = (uint32_t)(e->last_ms - e->first_ms) > MK_TRK_PERSIST_MS &&
                         (uint32_t)(now - e->last_ms) < MK_TRK_RECENT_MS;
        uint32_t bg = active ? MK_COL_ACCENT_DARK : MK_COL_ITEM_BG;
        mk_gfx_fill_round_rect(8, y, 304, 32, 4, bg);
        if (active) mk_gfx_fill_rect(8, y + 5, 3, 22, MK_COL_ACCENT);

        mk_gfx_text(17, y + 1, type_name(e->type), active ? MK_COL_ACCENT : MK_COL_TEXT);
        mk_snprintf(buf, sizeof(buf), "%02X:%02X:%02X #%lu %s",
                    e->mac[3], e->mac[4], e->mac[5], (unsigned long)e->id,
                    starting ? "wait" : !s.running ? "off" : !fresh ? "old"
                             : persistent ? "pers" : "live");
        mk_gfx_text(17, y + 17, buf, MK_COL_MUTED);

        mk_snprintf(buf, sizeof(buf), "%d dBm", (int)e->filtered_rssi);
        mk_gfx_text(242, y + 1, buf, fresh ? MK_COL_ACCENT : MK_COL_MUTED);
        mk_gfx_fill_rect(242, y + 23, 59, 4, MK_COL_BORDER);
        if (fresh) {
            int w = strength_for(e->filtered_rssi) * 59 / 100;
            if (w > 0) mk_gfx_fill_rect(242, y + 23, w, 4, MK_COL_ACCENT);
        }
    }
    mk_gfx_footer("Find", "Exit");
    mk_gfx_present();
}

/* ── Radar view (selected candidate) ────────────────────────────────────── */
static void draw_radar(void)
{
    mk_tracker_finder_t f; mk_tracker_finder(&f);
    char err[40]; int has_err = mk_tracker_error(err, sizeof(err));
    int starting = mk_tracker_starting();
    int running  = mk_tracker_running();

    int paused = !running || f.state == MK_TRK_PAUSED;
    int live   = !has_err && !starting && running && f.has_target &&
                 f.scan_fresh && f.state == MK_TRK_LIVE;

    mk_gfx_clear();
    mk_gfx_header("Proximity Radar");

    /* target identity */
    if (f.has_target) {
        char id[16]; mk_snprintf(id, sizeof(id), "#%lu", (unsigned long)f.target_id);
        mk_gfx_text(210, 6, id, MK_COL_MUTED);
    }
    mk_gfx_text(MK_PAD, 30, f.has_target ? type_name(f.type) : "Selected tracker", MK_COL_TEXT);
    if (f.has_target) {
        char mac[16]; mk_snprintf(mac, sizeof(mac), "%02X:%02X:%02X", f.mac[3], f.mac[4], f.mac[5]);
        mk_gfx_text(208, 30, mac, MK_COL_MUTED);
    }

    /* concentric strength rings (no bearing) */
    mk_gfx_text(36, 50, "SIGNAL /100", MK_COL_MUTED);
    int strength = f.strength < 0 ? 0 : f.strength > 100 ? 100 : f.strength;
    int radii[3] = { 50, 40, 30 };
    for (int i = 0; i < 3; i++) {
        int lit = live && strength >= (i + 1) * 25;
        mk_gfx_circle(80, 116, radii[i],     lit ? MK_COL_ACCENT     : MK_COL_BORDER);
        mk_gfx_circle(80, 116, radii[i] - 1, lit ? MK_COL_ACCENT_DIM : MK_COL_ITEM_BG);
    }
    if (live) {
        char sv[8]; mk_snprintf(sv, sizeof(sv), "%d", strength);
        int digits = strength >= 100 ? 3 : strength >= 10 ? 2 : 1;
        mk_gfx_text_sz(80 - digits * 6, 106, sv, MK_COL_ACCENT, 2);
    } else {
        mk_gfx_text_sz(66, 106, "--", MK_COL_MUTED, 2);
    }

    /* status + filtered rssi panel */
    const char* status = has_err ? "SCAN ERROR" : starting ? "STARTING" : paused ? "PAUSED" :
        !f.has_target ? "WAITING" : f.state == MK_TRK_LOST ? "LOST" : live ? "LIVE" : "WAITING";
    uint32_t scol = has_err ? MK_COL_ERR : live ? MK_COL_ACCENT : !paused ? MK_COL_WARN : MK_COL_MUTED;
    mk_gfx_fill_round_rect(148, 51, 164, 24, 5, MK_COL_ITEM_BG);
    mk_gfx_text(158, 57, status, scol);

    mk_gfx_text(148, 84, "FILTERED RSSI", MK_COL_MUTED);
    if (live) {
        char rv[10]; mk_snprintf(rv, sizeof(rv), "%d", (int)f.filtered_rssi);
        mk_gfx_text_sz(148, 96, rv, MK_COL_TEXT, 2);
    } else {
        mk_gfx_text_sz(148, 96, "--", MK_COL_MUTED, 2);
    }
    mk_gfx_text(216, 112, "dBm", MK_COL_MUTED);

    /* trend / hint line */
    const char* hint;
    if (has_err)            hint = err;
    else if (starting)      hint = "Starting BLE scan";
    else if (paused)        hint = "Scan is paused";
    else if (!live)         hint = "No fresh signal";
    else if (!f.trend_ready) hint = "Sampling trend...";
    else hint = f.trend > 0 ? "STRONGER" : f.trend < 0 ? "WEAKER" : "STEADY";
    mk_gfx_text(148, 136, hint, scol);

    char age[24];
    if (!f.has_target)         mk_snprintf(age, sizeof(age), "Waiting for target");
    else if (f.age_ms < 1000)  mk_snprintf(age, sizeof(age), "Seen <1s ago");
    else if (f.age_ms < 60000) mk_snprintf(age, sizeof(age), "Last %lus ago", (unsigned long)(f.age_ms / 1000));
    else                       mk_snprintf(age, sizeof(age), "Last %lum ago", (unsigned long)(f.age_ms / 60000));
    mk_gfx_text(148, 154, age, MK_COL_MUTED);

    /* history sparkline (fixed 48-sample window, right aligned) */
    mk_gfx_fill_round_rect(8, 172, 304, 25, 4, MK_COL_ITEM_BG);
    mk_gfx_text(13, 178, "RSSI", MK_COL_MUTED);
    mk_gfx_line(50, 185, 305, 185, MK_COL_BORDER);
    int count = f.history_count > MK_TRK_HISTORY ? MK_TRK_HISTORY : f.history_count;
    int px = 0, py = 0;
    uint32_t lc = live ? MK_COL_ACCENT : MK_COL_MUTED;
    for (int i = 0; i < count; i++) {
        int x = 50 + (48 - count + i) * 254 / 47;
        int y = 194 - strength_for(f.history[i]) * 18 / 100;
        if (i) mk_gfx_line(px, py, x, y, lc);
        else   mk_gfx_fill_rect(x, y, 2, 2, lc);
        px = x; py = y;
    }

    mk_gfx_footer(paused ? "Resume" : "Pause", "List");
    mk_gfx_present();
}

int app_main(int argc, char** argv)
{
    (void)argc; (void)argv;
    mk_tracker_begin();

    int screen = LIST, dirty = 1;
    uint32_t last = 0, last_poll = 0;

    for (;;) {
        mk_input_poll();
        mk_tracker_loop();
        uint32_t now = mk_millis();

        if (now - last_poll >= 250) {
            last_poll = now;
            if (screen == LIST) { g_n = mk_tracker_list(g_trk, MK_TRK_MAXTRK); reconcile(); }
            dirty = 1;
        }

        if (mk_btn_long(MK_BTN_B)) break;               /* hold B → exit */

        int a  = mk_btn(MK_BTN_A),  b  = mk_btn(MK_BTN_B);
        int up = mk_btn(MK_BTN_UP), dn = mk_btn(MK_BTN_DOWN);
        int lf = mk_btn(MK_BTN_LEFT), rt = mk_btn(MK_BTN_RIGHT);
        if (a||b||up||dn||lf||rt) dirty = 1;

        if (screen == LIST) {
            if (up && g_sel > 0)        { g_sel--; g_selid = g_trk[g_sel].id; }
            if (dn && g_sel < g_n - 1)  { g_sel++; g_selid = g_trk[g_sel].id; }
            if ((lf || rt)) { if (mk_tracker_running()) mk_tracker_pause(); else mk_tracker_resume(); }
            if (a && g_n > 0) { mk_tracker_select(g_trk[g_sel].id); screen = RADAR; }
        } else { /* RADAR */
            if (a) { if (mk_tracker_running()) mk_tracker_pause(); else mk_tracker_resume(); }
            if (b) { mk_tracker_select(0); screen = LIST; }
        }

        if (dirty || now - last >= 400) {
            if (screen == LIST) draw_list(); else draw_radar();
            last = now; dirty = 0;
        }
        mk_delay(20);
    }

    mk_tracker_stop();
    return 0;
}
