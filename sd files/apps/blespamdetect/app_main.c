/*
 * BLE Spam Detector — native MeowKit app (signed ELF, app SDK).
 *
 * Passively watches for the flood of Apple Continuity / Google FastPair /
 * Microsoft SwiftPair / Samsung EasySetup adverts that BLE-spam tools blast
 * out, via the firmware's BLE-spam monitor service. Listen-only.
 *
 * Controls: A pause/resume · hold B exit
 */
#include "mk_app_abi.h"

static uint16_t g_hist[MK_BLESPAM_HIST];

static void draw(void)
{
    mk_blespam_stats_t s;
    mk_blespam_stats(&s);
    int n = mk_blespam_history(g_hist, MK_BLESPAM_HIST);

    mk_gfx_clear();
    mk_gfx_header("BLE Spam Detect");

    const int by = MK_HDR_H + 6, bh = 36;
    uint32_t bg = s.alert ? MK_COL_ERR : (s.running ? MK_COL_ACCENT_DARK : MK_COL_ITEM_BG);
    uint32_t fg = s.alert ? MK_COL_WHITE : (s.running ? MK_COL_OK : MK_COL_MUTED);
    mk_gfx_fill_round_rect(8, by, MK_SCREEN_W - 16, bh, 6, bg);
    mk_gfx_text_sz(20, by + 6, !s.running ? "PAUSED" : (s.alert ? "ALERT!" : "CLEAR"), fg, 2);
    if (s.running && s.alert) mk_gfx_text(150, by + 12, "BLE spam flood!", MK_COL_WHITE);

    char buf[56];
    int y = by + bh + 6;
    mk_snprintf(buf, sizeof(buf), "now %u/s  peak %u  total %lu",
                s.rate, s.peak, (unsigned long)s.total);
    mk_gfx_text(MK_PAD, y, buf, s.rate >= MK_BLESPAM_THRESHOLD ? MK_COL_ERR : MK_COL_ACCENT);
    y += 20;
    mk_snprintf(buf, sizeof(buf), "Apple:%u Goog:%u MS:%u Sam:%u",
                s.apple, s.google, s.ms, s.samsung);
    mk_gfx_text(MK_PAD, y, buf, MK_COL_MUTED);

    const int gx = 10, gw = MK_SCREEN_W - 20, gy = 152, gh = 54;
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
                                 g_hist[i] >= MK_BLESPAM_THRESHOLD ? MK_COL_ERR : MK_COL_ACCENT);
        }
    }

    mk_gfx_footer(s.running ? "Pause" : "Start", "Exit");
    mk_gfx_present();
}

int app_main(int argc, char** argv)
{
    (void)argc; (void)argv;
    mk_blespam_begin();

    int dirty = 1;
    uint32_t last = 0;
    for (;;) {
        mk_input_poll();
        mk_blespam_loop();

        if (mk_btn_long(MK_BTN_B)) break;      /* hold B → exit */
        if (mk_btn(MK_BTN_A)) {
            if (mk_blespam_running()) mk_blespam_pause(); else mk_blespam_resume();
            dirty = 1;
        }

        uint32_t now = mk_millis();
        if (dirty || now - last >= 500) { draw(); last = now; dirty = 0; }
        mk_delay(20);
    }

    mk_blespam_stop();
    return 0;
}
