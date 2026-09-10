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

    if (scene && !strcmp(scene, "wlist")) {
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
