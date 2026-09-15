/*
 * Probe Sniffer — native MeowKit app (signed ELF, app SDK).
 *
 * Passively logs 802.11 probe-request frames (the network names nearby devices
 * leak while searching for known APs), via the firmware's probe monitor.
 * Listen-only; never transmits.
 *
 * Controls: A pause/resume · tap B list/graph · hold B exit
 */
#include "mk_app_abi.h"

static uint16_t       g_hist[MK_PROBE_HIST];
static mk_probe_dev_t g_dev[MK_PROBE_MAXDEV];

/* Small stats strip under the header (shared by both views). Returns next y. */
static int draw_stats(const mk_probe_stats_t* s)
{
    char buf[48];
    mk_snprintf(buf, sizeof(buf), "CH%-2d  %u dev  %lu probes",
                s->channel, s->devices, (unsigned long)s->total);
    mk_gfx_text(MK_PAD, MK_CONTENT_Y + 6, buf, s->running ? MK_COL_ACCENT : MK_COL_MUTED);
    return MK_CONTENT_Y + 28;
}

static void draw_list(void)
{
    mk_probe_stats_t s; mk_probe_stats(&s);
    int n = mk_probe_devices(g_dev, MK_PROBE_MAXDEV);

    mk_gfx_clear();
    mk_gfx_header("Probe Sniffer");
    int y = draw_stats(&s);

    if (n <= 0) {
        mk_gfx_text(MK_PAD, 110, "Listening for probe requests...", MK_COL_MUTED);
        mk_gfx_text(MK_PAD, 132, "Nearby devices will appear here.", MK_COL_MUTED);
        mk_gfx_footer(s.running ? "Pause" : "Start", "Graph");
        mk_gfx_present();
        return;
    }

    char buf[40];
    for (int i = 0; i < n && i < 7; i++) {
        mk_probe_dev_t* e = &g_dev[i];
        mk_snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                    e->mac[0], e->mac[1], e->mac[2], e->mac[3], e->mac[4], e->mac[5]);
        mk_gfx_text(MK_PAD, y, buf, MK_COL_TEXT);
        uint32_t rc = e->rssi > -50 ? MK_COL_OK : e->rssi > -70 ? MK_COL_WARN : MK_COL_MUTED;
        mk_snprintf(buf, sizeof(buf), "x%u %ddBm", e->count, e->rssi);
        mk_gfx_text(MK_SCREEN_W - 96, y, buf, rc);
        mk_gfx_text(MK_PAD + 8, y + 14, e->ssid[0] ? e->ssid : "(broadcast)",
                    e->ssid[0] ? MK_COL_ACCENT : MK_COL_MUTED);
        y += 26;
    }
    mk_gfx_footer(s.running ? "Pause" : "Start", "Graph");
    mk_gfx_present();
}

static void draw_graph(void)
{
    mk_probe_stats_t s; mk_probe_stats(&s);
    int n = mk_probe_history(g_hist, MK_PROBE_HIST);

    mk_gfx_clear();
    mk_gfx_header("Probe Sniffer");
    int y = draw_stats(&s);

    char buf[40];
    mk_snprintf(buf, sizeof(buf), "now %u /s   peak %u /s", s.rate, s.peak);
    mk_gfx_text(MK_PAD, y, buf, MK_COL_ACCENT);

    const int gx = 10, gw = MK_SCREEN_W - 20, gy = 150, gh = 60;
    mk_gfx_rect(gx - 1, gy - 1, gw + 2, gh + 2, MK_COL_BORDER);
    if (n > 0) {
        uint16_t mx = 1;
        for (int i = 0; i < n; i++) if (g_hist[i] > mx) mx = g_hist[i];
        int bw = gw / n; if (bw < 1) bw = 1;
        for (int i = 0; i < n; i++) {
            int h = (int)((unsigned)g_hist[i] * gh / mx);
            if (g_hist[i] > 0 && h < 2) h = 2;
            if (h > 0) mk_gfx_fill_rect(gx + i * bw, gy + gh - h, bw - 1, h, MK_COL_ACCENT);
        }
    }
    mk_gfx_text(gx, gy + gh + 3, "last 30s", MK_COL_MUTED);
    mk_gfx_footer(s.running ? "Pause" : "Start", "List");
    mk_gfx_present();
}

int app_main(int argc, char** argv)
{
    (void)argc; (void)argv;
    mk_wifi_begin();
    mk_probe_begin();

    int graph = 0, dirty = 1;
    uint32_t last = 0;
    for (;;) {
        mk_input_poll();
        mk_probe_loop();

        if (mk_btn_long(MK_BTN_B)) break;
        if (mk_btn(MK_BTN_A)) {
            if (mk_probe_running()) mk_probe_pause(); else mk_probe_resume();
            dirty = 1;
        }
        if (mk_btn(MK_BTN_B)) { graph = !graph; dirty = 1; }

        uint32_t now = mk_millis();
        if (dirty || now - last >= 500) {
            if (graph) draw_graph(); else draw_list();
            last = now; dirty = 0;
        }
        mk_delay(20);
    }

    mk_probe_stop();
    return 0;
}
