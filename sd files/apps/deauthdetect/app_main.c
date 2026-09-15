/*
 * Deauth Detector — native MeowKit app (signed ELF, app SDK).
 *
 * Passively watches for 802.11 deauth/disassoc bursts (a WiFi kick attack) via
 * the firmware's deauth monitor service. Listen-only; never transmits.
 *
 * Controls: A pause/resume · tap B graph/log · hold B exit
 */
#include "mk_app_abi.h"

static uint16_t      g_hist[MK_DEAUTH_HIST];
static mk_attacker_t g_atk[16];

static void draw_graph(void)
{
    mk_deauth_stats_t s;
    mk_deauth_stats(&s);
    int n = mk_deauth_history(g_hist, MK_DEAUTH_HIST);

    mk_gfx_clear();
    mk_gfx_header("Deauth Detector");

    /* Status banner */
    const int by = MK_HDR_H + 6, bh = 40;
    uint32_t bg = s.alert ? MK_COL_ERR : (s.running ? MK_COL_ACCENT_DARK : MK_COL_ITEM_BG);
    uint32_t fg = s.alert ? MK_COL_WHITE : (s.running ? MK_COL_OK : MK_COL_MUTED);
    mk_gfx_fill_round_rect(8, by, MK_SCREEN_W - 16, bh, 6, bg);
    mk_gfx_text_sz(20, by + 8, !s.running ? "PAUSED" : (s.alert ? "ALERT!" : "CLEAR"), fg, 2);
    if (s.running && s.alert) mk_gfx_text(150, by + 14, "deauth burst!", MK_COL_WHITE);

    /* Stats */
    char buf[48];
    int y = by + bh + 8;
    mk_snprintf(buf, sizeof(buf), "CH%-2d  total %lu  peak %u/s",
                s.channel, (unsigned long)s.total, s.peak);
    mk_gfx_text(MK_PAD, y, buf, MK_COL_MUTED);
    y += 20;
    mk_snprintf(buf, sizeof(buf), "now %u /s", s.rate);
    mk_gfx_text(MK_PAD, y, buf, s.rate >= MK_DEAUTH_THRESHOLD ? MK_COL_ERR : MK_COL_ACCENT);

    /* History bar graph */
    const int gx = 10, gw = MK_SCREEN_W - 20, gy = 150, gh = 60;
    mk_gfx_rect(gx - 1, gy - 1, gw + 2, gh + 2, MK_COL_BORDER);
    if (n > 0) {
        uint16_t mx = 1;
        for (int i = 0; i < n; i++) if (g_hist[i] > mx) mx = g_hist[i];
        int bw = gw / n; if (bw < 1) bw = 1;
        for (int i = 0; i < n; i++) {
            int h = (int)((unsigned)g_hist[i] * gh / mx);
            if (g_hist[i] > 0 && h < 2) h = 2;
            if (h > 0)
                mk_gfx_fill_rect(gx + i * bw, gy + gh - h, bw - 1, h,
                                 g_hist[i] >= MK_DEAUTH_THRESHOLD ? MK_COL_ERR : MK_COL_ACCENT);
        }
    }
    mk_gfx_text(gx, gy + gh + 3, "last 30s", MK_COL_MUTED);

    mk_gfx_footer(s.running ? "Pause" : "Start", "Log");
    mk_gfx_present();
}

static void draw_log(void)
{
    int running = mk_deauth_running();
    int n = mk_deauth_attackers(g_atk, 16);

    mk_gfx_clear();
    mk_gfx_header("Attacker Log");

    if (n <= 0) {
        mk_gfx_text(MK_PAD, 100, "No deauth sources seen yet.", MK_COL_MUTED);
        mk_gfx_text(MK_PAD, 122, "RSSI shows attacker proximity.", MK_COL_MUTED);
        mk_gfx_footer(running ? "Pause" : "Start", "Graph");
        mk_gfx_present();
        return;
    }

    int y = MK_CONTENT_Y + 6;
    char buf[40];
    for (int i = 0; i < n && i < 6; i++) {
        mk_attacker_t* e = &g_atk[i];
        mk_snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                    e->mac[0], e->mac[1], e->mac[2], e->mac[3], e->mac[4], e->mac[5]);
        mk_gfx_text(MK_PAD, y, buf, MK_COL_TEXT);
        mk_snprintf(buf, sizeof(buf), "x%u", e->count);
        mk_gfx_text(MK_SCREEN_W - 96, y, buf, MK_COL_ACCENT);
        uint32_t rc = e->rssi > -50 ? MK_COL_ERR : e->rssi > -70 ? MK_COL_WARN : MK_COL_MUTED;
        mk_snprintf(buf, sizeof(buf), "%ddBm", e->rssi);
        mk_gfx_text(MK_SCREEN_W - 56, y, buf, rc);
        y += 20;
    }
    mk_gfx_footer(running ? "Pause" : "Start", "Graph");
    mk_gfx_present();
}

int app_main(int argc, char** argv)
{
    (void)argc; (void)argv;
    mk_deauth_begin();

    int logview = 0, dirty = 1;
    uint32_t last = 0;

    for (;;) {
        mk_input_poll();
        mk_deauth_loop();

        if (mk_btn_long(MK_BTN_B)) break;                 /* hold B → exit */

        if (mk_btn(MK_BTN_A)) {
            if (mk_deauth_running()) mk_deauth_pause(); else mk_deauth_resume();
            dirty = 1;
        }
        if (mk_btn(MK_BTN_B)) { logview = !logview; dirty = 1; }  /* tap B → toggle view */

        uint32_t now = mk_millis();
        if (dirty || now - last >= 500) {                 /* ~2 Hz refresh */
            if (logview) draw_log(); else draw_graph();
            last = now; dirty = 0;
        }
        mk_delay(20);
    }

    mk_deauth_stop();
    return 0;
}
