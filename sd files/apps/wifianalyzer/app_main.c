/*
 * WiFi Analyzer — native MeowKit app (pilot for the app SDK).
 *
 * Pure C, built standalone against the mk_app ABI (mk_app_abi.h). No LVGL /
 * LovyanGFX / drivers linked in — every capability comes from the firmware's
 * exported mk_* symbols. Signed with tools/app_signing/meowsign.
 *
 * Controls (list):  Up/Down select · A details · tap B rescan · hold B exit
 *          (detail): B back
 */
#include "mk_app_abi.h"

#define MAXROWS 40

static mk_ap_t g_rows[MAXROWS];
static int     g_count = 0;

/* rssi(dBm) → 0..100 quality */
static int signal_pct(int rssi)
{
    int p = 2 * (rssi + 100);
    return p < 0 ? 0 : (p > 100 ? 100 : p);
}

static const char* auth_str(unsigned a)
{
    switch (a) {
        case 0:  return "Open";
        case 1:  return "WEP";
        case 2:  return "WPA";
        case 3:  return "WPA2";
        case 4:  return "WPA/2";
        case 5:  return "WPA2-EAP";
        case 6:  return "WPA3";
        case 7:  return "WPA2/3";
        default: return "WPA?";
    }
}

/* Descending sort by RSSI (insertion sort — small N, avoids libc qsort). */
static void sort_by_rssi(int n)
{
    for (int i = 1; i < n; i++) {
        mk_ap_t key = g_rows[i];
        int j = i - 1;
        while (j >= 0 && g_rows[j].rssi < key.rssi) {
            g_rows[j + 1] = g_rows[j];
            j--;
        }
        g_rows[j + 1] = key;
    }
}

static void draw_list(int sel, int scroll, int scanning)
{
    mk_gfx_clear();
    mk_gfx_header("WiFi Analyzer");

    if (scanning) {
        mk_gfx_text(8, 110, "Scanning 2.4GHz...", MK_COL_ACCENT);
        mk_gfx_footer("", "");
        mk_gfx_present();
        return;
    }
    if (g_count <= 0) {
        mk_gfx_text(8, 110, "No networks found", MK_COL_MUTED);
        mk_gfx_footer("Rescan", "Exit");
        mk_gfx_present();
        return;
    }

    int vis = mk_content_rows();
    for (int i = 0; i < vis; i++) {
        int idx = scroll + i;
        if (idx >= g_count) break;
        mk_ap_t* r = &g_rows[idx];
        char val[16];
        mk_snprintf(val, sizeof(val), "C%-2d %d%%%s",
                    r->channel, signal_pct(r->rssi), r->encrypted ? "*" : " ");
        const char* name = r->ssid[0] ? r->ssid : "<hidden>";
        mk_gfx_menu_item(i, name, val, idx == sel);
    }
    mk_gfx_footer("Details", "Rescan");
    mk_gfx_present();
}

static void draw_detail(const mk_ap_t* r)
{
    mk_gfx_clear();
    mk_gfx_header(r->ssid[0] ? r->ssid : "<hidden>");

    const int LX = 8, VX = 98;
    int y = 46;
    char buf[40];

    mk_gfx_text(LX, y, "BSSID", MK_COL_MUTED);
    mk_snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                r->bssid[0], r->bssid[1], r->bssid[2], r->bssid[3], r->bssid[4], r->bssid[5]);
    mk_gfx_text(VX, y, buf, MK_COL_TEXT); y += 28;

    mk_gfx_text(LX, y, "Channel", MK_COL_MUTED);
    mk_snprintf(buf, sizeof(buf), "%d  (2.4 GHz)", r->channel);
    mk_gfx_text(VX, y, buf, MK_COL_TEXT); y += 28;

    int pct = signal_pct(r->rssi);
    mk_gfx_text(LX, y, "Signal", MK_COL_MUTED);
    mk_snprintf(buf, sizeof(buf), "%d dBm  %d%%", r->rssi, pct);
    mk_gfx_text(VX, y, buf, pct >= 60 ? MK_COL_OK : pct >= 35 ? MK_COL_WARN : MK_COL_ERR); y += 28;

    mk_gfx_text(LX, y, "Security", MK_COL_MUTED);
    mk_gfx_text(VX, y, auth_str(r->auth), r->auth == 0 ? MK_COL_ERR : MK_COL_ACCENT); y += 28;

    mk_gfx_footer("", "Back");
    mk_gfx_present();
}

static void do_scan(void)
{
    draw_list(0, 0, 1);                 /* show "Scanning..." before blocking */
    int n = mk_wifi_scan(g_rows, MAXROWS);
    if (n < 0) n = 0;
    sort_by_rssi(n);
    g_count = n;
}

int app_main(int argc, char** argv)
{
    (void)argc; (void)argv;
    mk_wifi_begin();

    int sel = 0, scroll = 0, detail = 0, dirty = 1;
    do_scan();
    draw_list(sel, scroll, 0);

    for (;;) {
        mk_input_poll();

        if (!detail) {
            if (mk_btn_long(MK_BTN_B)) break;          /* hold B → exit app */

            int vis = mk_content_rows();
            if (mk_btn(MK_BTN_UP) && sel > 0) {
                sel--;
                if (sel < scroll) scroll = sel;
                dirty = 1;
            }
            if (mk_btn(MK_BTN_DOWN) && sel < g_count - 1) {
                sel++;
                if (sel >= scroll + vis) scroll = sel - vis + 1;
                dirty = 1;
            }
            if (mk_btn(MK_BTN_A) && g_count > 0) { detail = 1; dirty = 1; }
            if (mk_btn(MK_BTN_B)) { do_scan(); sel = 0; scroll = 0; dirty = 1; }
        } else {
            if (mk_btn(MK_BTN_B)) { detail = 0; dirty = 1; }
        }

        if (dirty) {
            if (detail) draw_detail(&g_rows[sel]);
            else        draw_list(sel, scroll, 0);
            dirty = 0;
        }
        mk_delay(20);
    }

    return 0;
}
