/*
 * Rogue Radar — native MeowKit app (signed ELF, app SDK).
 *
 * Two views:
 *   Twins  — scan APs and flag Evil-Twins (same SSID advertised both open and
 *            secured), computed on-device from mk_wifi_scan.
 *   Flood  — beacon/probe-response flood (Karma / rogue-AP) monitor service.
 *
 * Controls (twins):  Up/Down select · A rescan · tap B flood view · hold B exit
 *          (flood):  A pause/resume · tap B twins view · hold B exit
 */
#include "mk_app_abi.h"

#define MAXTW 40

typedef struct {
    char    ssid[33];
    uint8_t bssids;
    uint8_t open;
    uint8_t secure;
} twin_t;

static mk_ap_t   g_aps[MAXTW];
static twin_t    g_tw[MAXTW];
static int       g_tw_n = 0;
static int       g_flagged = 0;
static uint16_t  g_hist[MK_FLOOD_HIST];

static int is_twin(const twin_t* r) { return r->open && r->secure; }

/* strcmp without libc (avoid extra symbol) */
static int seq(const char* a, const char* b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static void scopy(char* dst, const char* src, int n)
{
    int i = 0;
    for (; i < n - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

static void do_scan(void)
{
    int n = mk_wifi_scan(g_aps, MAXTW);
    if (n < 0) n = 0;

    g_tw_n = 0;
    for (int i = 0; i < n; i++) {
        twin_t* row = 0;
        for (int j = 0; j < g_tw_n; j++)
            if (seq(g_tw[j].ssid, g_aps[i].ssid)) { row = &g_tw[j]; break; }
        if (!row) {
            if (g_tw_n >= MAXTW) continue;
            row = &g_tw[g_tw_n++];
            scopy(row->ssid, g_aps[i].ssid, (int)sizeof(row->ssid));
            row->bssids = 0; row->open = 0; row->secure = 0;
        }
        row->bssids++;
        if (g_aps[i].encrypted) row->secure = 1; else row->open = 1;
    }

    /* Twins first, then by BSSID count (insertion sort). */
    for (int i = 1; i < g_tw_n; i++) {
        twin_t key = g_tw[i];
        int j = i - 1;
        while (j >= 0) {
            int tj = is_twin(&g_tw[j]), tk = is_twin(&key);
            int gt = (tj != tk) ? (tk && !tj) : (key.bssids > g_tw[j].bssids);
            if (!gt) break;
            g_tw[j + 1] = g_tw[j]; j--;
        }
        g_tw[j + 1] = key;
    }
    g_flagged = 0;
    for (int i = 0; i < g_tw_n; i++) if (is_twin(&g_tw[i])) g_flagged++;
}

static void draw_twins(int sel, int scroll, int scanning)
{
    mk_gfx_clear();
    char hdr[28];
    mk_snprintf(hdr, sizeof(hdr), g_flagged > 0 ? "Evil-Twin: %d!" : "Evil-Twin Scan", g_flagged);
    mk_gfx_header(hdr);

    if (scanning) {
        mk_gfx_text(MK_PAD, 110, "Scanning...", MK_COL_ACCENT);
        mk_gfx_footer("Rescan", "Flood");
        mk_gfx_present();
        return;
    }
    if (g_tw_n <= 0) {
        mk_gfx_text(MK_PAD, 110, "No networks found", MK_COL_MUTED);
        mk_gfx_footer("Rescan", "Flood");
        mk_gfx_present();
        return;
    }

    int vis = mk_content_rows();
    for (int i = 0; i < vis; i++) {
        int idx = scroll + i;
        if (idx >= g_tw_n) break;
        twin_t* r = &g_tw[idx];
        const char* tag = is_twin(r) ? "TWIN!" : (r->bssids >= 3 ? "multi" : "");
        char val[16];
        mk_snprintf(val, sizeof(val), "x%u %s", r->bssids, tag);
        const char* name = r->ssid[0] ? r->ssid : "<hidden>";
        mk_gfx_menu_item(i, name, val, idx == sel);
        if (is_twin(r) && idx != sel)
            mk_gfx_fill_rect(0, MK_CONTENT_Y + i * MK_ITEM_H, 3, MK_ITEM_H, MK_COL_ERR);
    }
    mk_gfx_footer("Rescan", "Flood");
    mk_gfx_present();
}

static void draw_flood(void)
{
    mk_flood_stats_t s;
    mk_flood_stats(&s);
    int n = mk_flood_history(g_hist, MK_FLOOD_HIST);

    mk_gfx_clear();
    mk_gfx_header("Beacon Flood");

    const int by = MK_HDR_H + 6, bh = 36;
    uint32_t bg = s.alert ? MK_COL_ERR : (s.running ? MK_COL_ACCENT_DARK : MK_COL_ITEM_BG);
    uint32_t fg = s.alert ? MK_COL_WHITE : (s.running ? MK_COL_OK : MK_COL_MUTED);
    mk_gfx_fill_round_rect(8, by, MK_SCREEN_W - 16, bh, 6, bg);
    mk_gfx_text_sz(20, by + 6, !s.running ? "PAUSED" : (s.alert ? "FLOOD!" : "CLEAR"), fg, 2);
    if (s.running && s.alert) mk_gfx_text(150, by + 12, "rogue AP flood", MK_COL_WHITE);

    char buf[56];
    int y = by + bh + 6;
    mk_snprintf(buf, sizeof(buf), "APs/s %u  peak %u  probe %u", s.uniq, s.peak_uniq, s.proberesp);
    mk_gfx_text(MK_PAD, y, buf, s.uniq >= MK_FLOOD_ALERT_UNIQ ? MK_COL_ERR : MK_COL_ACCENT);
    y += 20;
    mk_snprintf(buf, sizeof(buf), "CH%-2d  frames/s %u", s.channel, s.rate);
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
                                 g_hist[i] >= MK_FLOOD_ALERT_UNIQ ? MK_COL_ERR : MK_COL_ACCENT);
        }
    }

    mk_gfx_footer(s.running ? "Pause" : "Start", "Twins");
    mk_gfx_present();
}

int app_main(int argc, char** argv)
{
    (void)argc; (void)argv;
    mk_wifi_begin();

    int mode_flood = 0;          /* 0 = twins, 1 = flood */
    int sel = 0, scroll = 0, dirty = 1;
    uint32_t last = 0;

    draw_twins(0, 0, 1);
    do_scan();
    dirty = 1;

    for (;;) {
        mk_input_poll();
        if (mk_btn_long(MK_BTN_B)) break;      /* hold B → exit */

        if (!mode_flood) {
            int vis = mk_content_rows();
            if (mk_btn(MK_BTN_UP) && sel > 0) {
                sel--; if (sel < scroll) scroll = sel; dirty = 1;
            }
            if (mk_btn(MK_BTN_DOWN) && sel < g_tw_n - 1) {
                sel++; if (sel >= scroll + vis) scroll = sel - vis + 1; dirty = 1;
            }
            if (mk_btn(MK_BTN_A)) { draw_twins(sel, scroll, 1); do_scan(); sel = 0; scroll = 0; dirty = 1; }
            if (mk_btn(MK_BTN_B)) { mode_flood = 1; mk_flood_begin(); dirty = 1; }
            if (dirty) { draw_twins(sel, scroll, 0); dirty = 0; }
        } else {
            mk_flood_loop();
            if (mk_btn(MK_BTN_A)) {
                if (mk_flood_running()) mk_flood_pause(); else mk_flood_resume();
                dirty = 1;
            }
            if (mk_btn(MK_BTN_B)) { mk_flood_stop(); mode_flood = 0; do_scan(); sel = 0; scroll = 0; dirty = 1; }
            uint32_t now = mk_millis();
            if (dirty || now - last >= 500) { draw_flood(); last = now; dirty = 0; }
        }
        mk_delay(20);
    }

    mk_flood_stop();
    return 0;
}
