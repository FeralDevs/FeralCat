/*
 * Tracker Detector — native MeowKit app (signed ELF, app SDK).
 *
 * Passively scans BLE for item trackers (Apple Find My / AirTag, Tile, Samsung
 * SmartTag) and flags any that have followed you long enough to matter, via the
 * firmware's tracker monitor. Listen-only.
 *
 * Controls: A pause/resume · hold B exit
 */
#include "mk_app_abi.h"

static mk_tracker_t g_trk[MK_TRK_MAXTRK];

static const char* type_name(unsigned t)
{
    switch (t) {
        case MK_TRK_APPLE:   return "AirTag / Find My";
        case MK_TRK_TILE:    return "Tile";
        case MK_TRK_SAMSUNG: return "Samsung SmartTag";
        default:             return "Tracker";
    }
}

static void draw(void)
{
    mk_tracker_stats_t s; mk_tracker_stats(&s);
    int n = mk_tracker_list(g_trk, MK_TRK_MAXTRK);
    uint32_t now = mk_millis();

    mk_gfx_clear();
    mk_gfx_header("Tracker Detector");

    /* Status banner */
    const int by = MK_HDR_H + 6, bh = 40;
    uint32_t bg = s.alert ? MK_COL_ERR : (s.running ? MK_COL_ACCENT_DARK : MK_COL_ITEM_BG);
    uint32_t fg = s.alert ? MK_COL_WHITE : (s.running ? MK_COL_OK : MK_COL_MUTED);
    mk_gfx_fill_round_rect(8, by, MK_SCREEN_W - 16, bh, 6, bg);
    const char* msg = !s.running ? "PAUSED" : (s.alert ? "FOLLOWED!" : (s.nearby ? "NEARBY" : "CLEAR"));
    mk_gfx_text_sz(20, by + 8, msg, fg, 2);
    if (s.running) {
        char b[32];
        mk_snprintf(b, sizeof(b), "%u seen  %u persist", s.nearby, s.persistent);
        mk_gfx_text(180, by + 14, b, fg);
    }

    if (n <= 0) {
        mk_gfx_text(MK_PAD, 110, "No trackers detected.", MK_COL_MUTED);
        mk_gfx_text(MK_PAD, 132, "AirTag / Tile / SmartTag show here.", MK_COL_MUTED);
        mk_gfx_footer(s.running ? "Pause" : "Start", "Exit");
        mk_gfx_present();
        return;
    }

    int y = by + bh + 10;
    char buf[40];
    for (int i = 0; i < n && i < 5; i++) {
        mk_tracker_t* e = &g_trk[i];
        uint32_t seen_s = (e->last_ms - e->first_ms) / 1000;
        int persistent = (e->last_ms - e->first_ms) > MK_TRK_PERSIST_MS &&
                         (now - e->last_ms) < MK_TRK_RECENT_MS;

        mk_gfx_text(MK_PAD, y, type_name(e->type), persistent ? MK_COL_ERR : MK_COL_TEXT);
        uint32_t rc = e->rssi > -55 ? MK_COL_ERR : e->rssi > -75 ? MK_COL_WARN : MK_COL_MUTED;
        mk_snprintf(buf, sizeof(buf), "%ddBm", e->rssi);
        mk_gfx_text(MK_SCREEN_W - 56, y, buf, rc);

        if (seen_s >= 60)
            mk_snprintf(buf, sizeof(buf), "seen %lum %lus%s",
                        (unsigned long)(seen_s / 60), (unsigned long)(seen_s % 60),
                        persistent ? "  FOLLOWING" : "");
        else
            mk_snprintf(buf, sizeof(buf), "seen %lus%s",
                        (unsigned long)seen_s, persistent ? "  FOLLOWING" : "");
        mk_gfx_text(MK_PAD + 8, y + 14, buf, persistent ? MK_COL_ERR : MK_COL_MUTED);
        y += 26;
    }
    mk_gfx_footer(s.running ? "Pause" : "Start", "Exit");
    mk_gfx_present();
}

int app_main(int argc, char** argv)
{
    (void)argc; (void)argv;
    mk_tracker_begin();

    int dirty = 1;
    uint32_t last = 0;
    for (;;) {
        mk_input_poll();
        mk_tracker_loop();

        if (mk_btn_long(MK_BTN_B)) break;
        if (mk_btn(MK_BTN_A)) {
            if (mk_tracker_running()) mk_tracker_pause(); else mk_tracker_resume();
            dirty = 1;
        }

        uint32_t now = mk_millis();
        if (dirty || now - last >= 500) { draw(); last = now; dirty = 0; }
        mk_delay(20);
    }

    mk_tracker_stop();
    return 0;
}
