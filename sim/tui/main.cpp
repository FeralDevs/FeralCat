/* Headless renderer for MeowKit LovyanGFX "TUI" app screens.
 *
 * The hacker-tool apps (BLE Spam, Bad USB, and the new WiFi apps) draw straight
 * to a LovyanGFX surface via the MK_TUI toolkit instead of LVGL, so the LVGL
 * simulator can't see them. This renders those same MK_TUI primitives into an
 * off-screen 320x240 sprite (no window, no display server) and writes a BMP,
 * so app screens can be reviewed on a PC.
 */
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "mk_tui.h"
#include "app_10/meowgotchi_ui.h"
#include "app_11/wifi_analyzer_ui.h"
#include "app_13/deauth_monitor.h"
#include "app_13/deauth_ui.h"
#include "app_14/ble_spam_monitor.h"
#include "app_14/ble_spam_ui.h"
#include "app_15/beacon_flood.h"
#include "app_15/rogue_ui.h"
#include "app_16/probe_monitor.h"
#include "app_16/probe_ui.h"
#include "app_17/tracker_monitor.h"
#include "app_17/tracker_ui.h"

/* Write an RGB565 framebuffer as a 24-bit BMP (bottom-up, BGR). */
static int save_bmp565(const char* path, const uint16_t* fb, int W, int H)
{
    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "open %s failed\n", path); return 1; }
    const int row = W * 3;
    const int pad = (4 - (row % 4)) % 4;
    const int img = (row + pad) * H;
    const int off = 54;
    uint8_t hdr[54] = {0};
    hdr[0]='B'; hdr[1]='M';
    uint32_t fsz = off + img;
    memcpy(&hdr[2], &fsz, 4);
    uint32_t o = off; memcpy(&hdr[10], &o, 4);
    uint32_t ih = 40; memcpy(&hdr[14], &ih, 4);
    int32_t w = W, h = H; memcpy(&hdr[18], &w, 4); memcpy(&hdr[22], &h, 4);
    uint16_t planes = 1, bpp = 24; memcpy(&hdr[26], &planes, 2); memcpy(&hdr[28], &bpp, 2);
    fwrite(hdr, 1, 54, f);
    uint8_t* line = (uint8_t*)calloc(1, row + pad);
    for (int y = H - 1; y >= 0; y--) {
        for (int x = 0; x < W; x++) {
            uint16_t p = fb[y * W + x];
            p = (uint16_t)((p >> 8) | (p << 8));   /* LGFX sprite stores RGB565 byte-swapped */
            uint8_t r = ((p >> 11) & 0x1F) << 3;
            uint8_t g = ((p >> 5)  & 0x3F) << 2;
            uint8_t b = ( p        & 0x1F) << 3;
            line[x*3+0] = b; line[x*3+1] = g; line[x*3+2] = r;
        }
        fwrite(line, 1, row + pad, f);
    }
    free(line);
    fclose(f);
    printf("wrote %s (%dx%d)\n", path, W, H);
    return 0;
}

/* Named MeowGotchi mood scenes so `--scene <name>` renders any one of them. */
struct Scene { const char* name; MeowGotchi::View view; };

static Scene SCENES[] = {
    { "sleep",   { MeowGotchi::Mood::Sleep,   1,  0,  0, 0, 0,   4, false, false, nullptr } },
    { "hunt",    { MeowGotchi::Mood::Hunt,    6, 12,  5, 0, 0, 137, false, false, nullptr } },
    { "excited", { MeowGotchi::Mood::Excited, 9, 21, 14, 3, 0, 402, false, false, nullptr } },
    { "cool",    { MeowGotchi::Mood::Cool,   11, 33, 22, 5, 88, 915, true,  false, nullptr } },
    { "bored",   { MeowGotchi::Mood::Bored,   3,  8,  2, 0, 0, 640, false, false, nullptr } },
    { "sad",     { MeowGotchi::Mood::Sad,     1,  4,  1, 0, 0,  12, false, false, nullptr } },
};
static const int SCENE_COUNT = (int)(sizeof(SCENES) / sizeof(SCENES[0]));

int main(int argc, char** argv)
{
    const char* out   = "tui.bmp";
    const char* scene = nullptr;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--scene") && i + 1 < argc) scene = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--list")) {
            for (int k = 0; k < SCENE_COUNT; k++) printf("%s\n", SCENES[k].name);
            return 0;
        } else out = argv[i];   /* positional output path */
    }

    lgfx::LGFX_Sprite canvas;
    canvas.setColorDepth(16);
    if (!canvas.createSprite(320, 240)) {
        fprintf(stderr, "sprite alloc failed\n");
        return 1;
    }

    static WifiAnalyzer::Row wrows[] = {
        { "HomeNet-5G",        -42,  6, true,  4, {0x3c,0x84,0x6a,0x11,0x22,0x33} },
        { "Freebox-8A2C1D",    -55, 11, true,  6, {0xf4,0xca,0xe5,0xaa,0xbb,0xcc} },
        { "SFR_1A2B",          -68,  1, true,  3, {0x00,0x1e,0x2a,0x44,0x55,0x66} },
        { "xfinitywifi",       -74,  6, false, 0, {0x12,0x34,0x56,0x78,0x9a,0xbc} },
        { "un-ssid-tres-long", -80,  9, true,  7, {0xde,0xad,0xbe,0xef,0x00,0x01} },
        { "",                  -85,  3, true,  2, {0xaa,0xaa,0xaa,0xaa,0xaa,0xaa} },
    };
    const int wn = 6;

    if (scene && !strcmp(scene, "deauth_log")) {
        static AttackerEntry atk[] = {
            { {0x3c,0x84,0x6a,0x11,0x22,0x33}, {0xff,0xff,0xff,0xff,0xff,0xff}, 84, -41, 6 },
            { {0xf4,0xca,0xe5,0xaa,0xbb,0xcc}, {0x12,0x34,0x56,0x78,0x9a,0xbc}, 37, -63, 11 },
            { {0x00,0x1e,0x2a,0x44,0x55,0x66}, {0xde,0xad,0xbe,0xef,0x00,0x01}, 9,  -78, 1 },
        };
        DeauthUI::drawLog(canvas, atk, 3, true);
    } else if (scene && !strcmp(scene, "twins")) {
        static RogueUI::TwinRow tw[] = {
            { "FreeCoffeeWiFi", 2, true,  true  },   /* open + secure = TWIN */
            { "CorpNet",        3, false, true  },   /* multi */
            { "HomeNet-5G",     1, false, true  },
            { "xfinitywifi",    1, true,  false },
        };
        RogueUI::drawTwins(canvas, tw, 4, 0, 0, false, 1);
    } else if (scene && (!strcmp(scene, "flood_clear") || !strcmp(scene, "flood_alert"))) {
        static uint16_t hist[BeaconFlood::HIST];
        bool alert = !strcmp(scene, "flood_alert");
        for (int i = 0; i < BeaconFlood::HIST; i++) hist[i] = 3 + (i % 5);
        if (alert) for (int i = 22; i < 30; i++) hist[i] = 30 + (i % 6) * 4;
        RogueUI::FloodView v;
        v.s.channel = 6; v.s.rate = alert ? 210 : 42;
        v.s.uniq = alert ? 41 : 8; v.s.peak_uniq = alert ? 48 : 9;
        v.s.proberesp = alert ? 120 : 3; v.s.alert = alert;
        v.running = true;
        v.hist = hist; v.histLen = BeaconFlood::HIST;
        v.threshold = BeaconFlood::ALERT_UNIQ;
        RogueUI::drawFlood(canvas, v);
    } else if (scene && (!strcmp(scene, "ble_clear") || !strcmp(scene, "ble_alert"))) {
        static uint16_t hist[BleSpamMonitor::HIST];
        bool alert = !strcmp(scene, "ble_alert");
        for (int i = 0; i < BleSpamMonitor::HIST; i++) hist[i] = (i % 9 == 0) ? 2 : 0;
        if (alert) { for (int i = 24; i < 30; i++) hist[i] = 18 + (i % 4) * 6; }
        BleSpamUI::View v;
        v.total   = alert ? 642 : 7;
        v.rate    = alert ? 24 : 1;
        v.peak    = alert ? 36 : 3;
        v.alert   = alert;
        v.running = true;
        v.apple   = alert ? 410 : 4;
        v.google  = alert ? 120 : 1;
        v.ms      = alert ? 72  : 2;
        v.samsung = alert ? 40  : 0;
        v.hist = hist; v.histLen = BleSpamMonitor::HIST;
        v.threshold = BleSpamMonitor::ALERT_THRESHOLD;
        BleSpamUI::draw(canvas, v);
    } else if (scene && (!strcmp(scene, "deauth_clear") || !strcmp(scene, "deauth_alert"))) {
        static uint16_t hist[DeauthMonitor::HIST];
        bool alert = !strcmp(scene, "deauth_alert");
        for (int i = 0; i < DeauthMonitor::HIST; i++)
            hist[i] = (i % 7 == 0) ? 1 : 0;                 /* quiet background */
        if (alert) { hist[26]=9; hist[27]=14; hist[28]=11; hist[29]=7; }
        DeauthUI::View v;
        v.channel = alert ? 6 : 11;
        v.total   = alert ? 128 : 3;
        v.rate    = alert ? 7 : 0;
        v.peak    = alert ? 14 : 1;
        v.alert   = alert;
        v.running = true;
        v.hist = hist; v.histLen = DeauthMonitor::HIST;
        v.threshold = DeauthMonitor::ALERT_THRESHOLD;
        DeauthUI::draw(canvas, v);
    } else if (scene && !strcmp(scene, "probe_list")) {
        static uint16_t hist[ProbeMonitor::HIST];
        for (int i = 0; i < ProbeMonitor::HIST; i++) hist[i] = 1 + (i % 4);
        static ProbeEntry dev[] = {
            { {0x3c,0x84,0x6a,0x11,0x22,0x33}, "HomeNet-5G",   38, -47, 6 },
            { {0xa4,0x83,0xe7,0x9a,0xbc,0xde}, "Starbucks",    21, -61, 1 },
            { {0xf0,0x18,0x98,0x44,0x55,0x66}, "",             15, -70, 11 },
            { {0x00,0x1e,0x2a,0x77,0x88,0x99}, "iPhone-Tom",    7, -78, 9 },
        };
        ProbeUI::View v;
        v.channel = 6; v.total = 812; v.rate = 14; v.peak = 33;
        v.devices = 4; v.running = true;
        v.hist = hist; v.histLen = ProbeMonitor::HIST;
        ProbeUI::drawList(canvas, v, dev, 4);
    } else if (scene && (!strcmp(scene, "tracker_list") || !strcmp(scene, "tracker_alert"))) {
        bool alert = !strcmp(scene, "tracker_alert");
        uint32_t now = 200000;
        static TrackerEntry trk[] = {
            { {0x4c,0x00,0x12,0xaa,0xbb,0xcc}, TRK_APPLE,   -46, 220, now - 140000, now - 2000 },
            { {0xfe,0xed,0x00,0x11,0x22,0x33}, TRK_TILE,    -68,  40, now - 30000,  now - 5000 },
            { {0xfd,0x5a,0x00,0x44,0x55,0x66}, TRK_SAMSUNG, -80,  12, now - 12000,  now - 8000 },
        };
        TrackerUI::View v;
        v.nearby = 3; v.persistent = alert ? 1 : 0; v.alert = alert;
        v.running = true; v.now_ms = now;
        TrackerUI::drawList(canvas, v, trk, 3);
    } else if (scene && !strcmp(scene, "splash")) {
        canvas.fillScreen(0x0000);
        uint32_t lime  = canvas.color888(0xC4, 0xEE, 0x1F);
        uint32_t white = canvas.color888(0xE9, 0xEF, 0xE2);
        uint32_t muted = canvas.color888(0x9A, 0xA3, 0x94);
        uint32_t track = canvas.color888(0x2A, 0x2D, 0x26);
        canvas.setFont(&fonts::efontCN_24);
        canvas.setTextColor(lime, 0x0000);
        int tw = canvas.textWidth("MeowGotchi");
        canvas.setCursor((320 - tw) / 2, 86); canvas.print("MeowGotchi");
        canvas.setFont(&fonts::efontCN_16);
        canvas.setTextColor(white, 0x0000);
        tw = canvas.textWidth("v0.6.0");
        canvas.setCursor((320 - tw) / 2, 118); canvas.print("v0.6.0");
        canvas.setTextColor(muted, 0x0000);
        tw = canvas.textWidth("Fork: Janud");
        canvas.setCursor((320 - tw) / 2, 138); canvas.print("Fork: Janud");
        int bw = 220, bh = 12, bx = (320 - bw) / 2, by = 176;
        canvas.drawRoundRect(bx - 2, by - 2, bw + 4, bh + 4, 4, track);
        canvas.fillRoundRect(bx, by, bw * 65 / 100, bh, 3, lime);
        /* stage label under the bar (as step() draws it) */
        canvas.setTextColor(muted, 0x0000);
        tw = canvas.textWidth("Display");
        canvas.setCursor((320 - tw) / 2, by + bh + 10); canvas.print("Display");
    } else if (scene && !strcmp(scene, "firmware_menu")) {
        MK_TUI::clearScreen(canvas);
        MK_TUI::drawHeader(canvas, "Firmware");
        MK_TUI::drawMenuItem(canvas, 0, "Update over WiFi",   "from GitHub",  true);
        MK_TUI::drawMenuItem(canvas, 1, "Update from SD",     "firmware.bin", false);
        MK_TUI::drawMenuItem(canvas, 2, "USB Download Mode",  "flash via PC", false);
        MK_TUI::drawFooter(canvas, "Select", "Exit");
    } else if (scene && !strcmp(scene, "wifi_download")) {
        MK_TUI::clearScreen(canvas);
        MK_TUI::drawHeader(canvas, "WiFi Update");
        canvas.setFont(&fonts::efontCN_16);
        canvas.setTextColor((uint32_t)MK_PAL::TEXT_PRI, (uint32_t)MK_PAL::BLACK);
        canvas.setCursor(MK_LAYOUT::PAD, MK_LAYOUT::CONTENT_Y + 16);
        canvas.printf("Downloading v0.4.0");
        MK_TUI::drawProgress(canvas, MK_LAYOUT::CONTENT_Y + 80, 47, nullptr);
        canvas.setTextColor((uint32_t)MK_PAL::ACCENT, (uint32_t)MK_PAL::BLACK);
        canvas.setCursor(MK_LAYOUT::W - 60, MK_LAYOUT::CONTENT_Y + 96);
        canvas.printf("%3d%%", 47);
    } else if (scene && !strcmp(scene, "wifi_avail")) {
        MK_TUI::clearScreen(canvas);
        MK_TUI::drawHeader(canvas, "WiFi Update");
        canvas.setFont(&fonts::efontCN_16);
        canvas.setTextColor((uint32_t)MK_PAL::TEXT_PRI, (uint32_t)MK_PAL::BLACK);
        canvas.setCursor(MK_LAYOUT::PAD, 96);
        canvas.printf("New version available");
        canvas.setTextColor((uint32_t)MK_PAL::ACCENT, (uint32_t)MK_PAL::BLACK);
        canvas.setCursor(MK_LAYOUT::PAD, 118);
        canvas.printf("v0.3.0  ->  v0.4.0");
        MK_TUI::drawFooter(canvas, "Download", "Cancel");
    } else if (scene && !strcmp(scene, "sd_progress")) {
        MK_TUI::clearScreen(canvas);
        MK_TUI::drawHeader(canvas, "SD Update");
        canvas.setFont(&fonts::efontCN_16);
        canvas.setTextColor((uint32_t)MK_PAL::TEXT_PRI, (uint32_t)MK_PAL::BLACK);
        canvas.setCursor(MK_LAYOUT::PAD, MK_LAYOUT::CONTENT_Y + 16);
        canvas.printf("Writing firmware...");
        canvas.setTextColor((uint32_t)MK_PAL::ERR, (uint32_t)MK_PAL::BLACK);
        canvas.setCursor(MK_LAYOUT::PAD, MK_LAYOUT::CONTENT_Y + 38);
        canvas.printf("Do NOT power off.");
        MK_TUI::drawProgress(canvas, MK_LAYOUT::CONTENT_Y + 80, 62, nullptr);
        canvas.setTextColor((uint32_t)MK_PAL::ACCENT, (uint32_t)MK_PAL::BLACK);
        canvas.setCursor(MK_LAYOUT::W - 60, MK_LAYOUT::CONTENT_Y + 96);
        canvas.printf("%3d%%", 62);
    } else if (scene && !strcmp(scene, "wlist")) {
        WifiAnalyzer::drawList(canvas, wrows, wn, 1, 0, false);
    } else if (scene && !strcmp(scene, "wdetail")) {
        WifiAnalyzer::drawDetail(canvas, wrows[1]);
    } else if (scene && !strcmp(scene, "menu")) {
        /* Preview of App10's start menu (mirrors _renderMenu). */
        MK_TUI::clearScreen(canvas);
        MK_TUI::drawHeader(canvas, "MeowGotchi");
        MK_TUI::drawMenuItem(canvas, 0, "Hunt", "START", true);
        MK_TUI::drawMenuItem(canvas, 1, "Mode", "passive", false);
        MK_TUI::drawMenuItem(canvas, 2, "Handshakes", "0", false);
        MK_TUI::drawFooter(canvas, "Select", "Back");
    } else {
        const Scene* sel = &SCENES[0];
        if (scene) {
            for (int k = 0; k < SCENE_COUNT; k++)
                if (!strcmp(SCENES[k].name, scene)) { sel = &SCENES[k]; break; }
        }
        MeowGotchi::drawFace(canvas, sel->view);
    }

    const uint16_t* fb = (const uint16_t*)canvas.getBuffer();
    return save_bmp565(out, fb, 320, 240);
}
