/* Simulateur de bureau pour l'UI du MeowKit-S3.
 *
 * Rend l'interface LVGL du firmware dans une fenêtre SDL2 en 320×240, la
 * résolution exacte de l'écran ST7789. La souris tient lieu de dalle tactile
 * (FT6336).
 *
 *   ./meowkit-sim                          fenêtre interactive
 *   ./meowkit-sim --screen tabview         démarre sur un écran précis
 *   ./meowkit-sim --screen wifi --shot a.bmp   capture hors écran, puis sortie
 *   ./meowkit-sim --list                   liste les écrans disponibles
 *
 * Le mode capture force le pilote vidéo « dummy » : il fonctionne sans serveur
 * graphique, ce qui permet de vérifier un correctif d'affichage en CI ou
 * depuis un terminal.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include "lvgl.h"
#include "ui.h"

/* UI de l'app PC Monitor (src/app/app_05/asset) — déclarée à la main pour
 * éviter un conflit de noms entre les deux « ui.h » du projet. */
void ui_PC_Monitor_screen_init(void);
extern lv_obj_t *ui_PC_Monitor;

#define HOR_RES 320
#define VER_RES 240
#define SCALE   3          /* fenêtre agrandie ×3, sinon c'est un timbre-poste */

static uint16_t      framebuffer[HOR_RES * VER_RES];
static SDL_Window   *window;
static SDL_Renderer *renderer;
static SDL_Texture  *texture;

/* LVGL nous rend une zone rectangulaire ; on la recopie dans le framebuffer.
 * LV_COLOR_DEPTH vaut 16 et LV_COLOR_16_SWAP 0 → RGB565 natif, pas de
 * conversion nécessaire. */
static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    for (int y = area->y1; y <= area->y2; y++) {
        for (int x = area->x1; x <= area->x2; x++) {
            if (x >= 0 && x < HOR_RES && y >= 0 && y < VER_RES) {
                framebuffer[y * HOR_RES + x] = color_p->full;
            }
            color_p++;
        }
    }
    lv_disp_flush_ready(drv);
}

/* La souris simule le tactile capacitif. */
static void mouse_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    int x = 0, y = 0;
    Uint32 buttons = SDL_GetMouseState(&x, &y);
    data->point.x = (lv_coord_t)(x / SCALE);
    data->point.y = (lv_coord_t)(y / SCALE);
    data->state   = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT))
                        ? LV_INDEV_STATE_PRESSED
                        : LV_INDEV_STATE_RELEASED;
}

static void present(void)
{
    SDL_UpdateTexture(texture, NULL, framebuffer, HOR_RES * sizeof(uint16_t));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

static int save_bmp(const char *path)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(
        framebuffer, HOR_RES, VER_RES, 16, HOR_RES * sizeof(uint16_t),
        SDL_PIXELFORMAT_RGB565);
    if (!s) { fprintf(stderr, "surface: %s\n", SDL_GetError()); return 1; }
    int rc = SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
    if (rc != 0) { fprintf(stderr, "SDL_SaveBMP: %s\n", SDL_GetError()); return 1; }
    printf("capture écrite : %s (%dx%d)\n", path, HOR_RES, VER_RES);
    return 0;
}

/* Table des écrans : chaque entrée associe un nom de ligne de commande à sa
 * fonction d'initialisation et à l'objet racine à charger. Permet de capturer
 * n'importe quel écran sans naviguer à la souris. */
typedef struct {
    const char *name;
    void      (*init)(void);
    lv_obj_t **screen;
} screen_entry_t;

static const screen_entry_t SCREENS[] = {
    { "home",          ui_home_screen_init,          &ui_home          },
    { "apps",          ui_apps_menu_screen_init,     &ui_apps_menu     },
    { "clock",         ui_clock_screen_init,         &ui_clock         },
    { "settings",      ui_settings_screen_init,      &ui_settings      },
    { "tabview",       ui_tabview_screen_init,       &ui_tabview       },
    { "wifi",          ui_wifi_screen_init,          &ui_wifi          },
    { "files",         ui_sd_card_files_screen_init, &ui_sd_card_files },
    { "manual",        ui_manual_screen_init,        &ui_manual        },
    { "usb_msc",       ui_usb_msc_screen_init,       &ui_usb_msc       },
    { "update",        ui_update_screen_init,        &ui_update        },
    { "t9",            ui_t9_keyboard_screen_init,   &ui_t9_keyboard   },
    { "pcmon",         ui_PC_Monitor_screen_init,    &ui_PC_Monitor    },
};
#define SCREEN_COUNT ((int)(sizeof(SCREENS) / sizeof(SCREENS[0])))

/* Build the Settings "About" modal overlay (mock data) for a screenshot. */
static void build_about_overlay(void)
{
    const char *body =
        "MeowGotchi v0.5.0\n"
        "Fork: Janud\n"
        "SoC: ESP32-S3  N16R8\n"
        "Flash 16MB DIO  PSRAM 8MB\n"
        "MAC 3C:84:6A:11:22:33\n"
        "Free heap: 210 KB\n"
        "SD card: ready";

    lv_obj_t *ov = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(ov);
    lv_obj_set_size(ov, 320, 240);
    lv_obj_center(ov);
    lv_obj_set_style_bg_color(ov, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_60, 0);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel = lv_obj_create(ov);
    lv_obj_set_size(panel, 280, 190);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0xBBE700), 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "About");
    lv_obj_set_style_text_color(title, lv_color_hex(0xBBE700), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *lbl = lv_label_create(panel);
    lv_label_set_text(lbl, body);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 22);

    lv_obj_t *close = lv_btn_create(panel);
    lv_obj_set_size(close, 90, 30);
    lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, 4);
    lv_obj_set_style_bg_color(close, lv_color_hex(0xBBE700), 0);
    lv_obj_t *cl = lv_label_create(close);
    lv_label_set_text(cl, "Close");
    lv_obj_set_style_text_color(cl, lv_color_hex(0x000000), 0);
    lv_obj_center(cl);
}

int main(int argc, char **argv)
{
    const char *shot_path   = NULL;
    const char *screen_name = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--shot")   == 0 && i + 1 < argc) shot_path   = argv[++i];
        else if (strcmp(argv[i], "--screen") == 0 && i + 1 < argc) screen_name = argv[++i];
        else if (strcmp(argv[i], "--list") == 0) {
            printf("Écrans disponibles :\n");
            for (int k = 0; k < SCREEN_COUNT; k++) printf("  %s\n", SCREENS[k].name);
            return 0;
        }
    }
    if (shot_path) SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    window = SDL_CreateWindow("MeowKit-S3 — simulateur UI",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              HOR_RES * SCALE, VER_RES * SCALE, 0);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    texture  = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                 SDL_TEXTUREACCESS_STREAMING, HOR_RES, VER_RES);
    if (!window || !renderer || !texture) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }

    lv_init();

    static lv_disp_draw_buf_t draw_buf;
    static lv_color_t         buf[HOR_RES * 40];
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, HOR_RES * 40);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = HOR_RES;
    disp_drv.ver_res  = VER_RES;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = mouse_read_cb;
    lv_indev_drv_register(&indev_drv);

    ui_init();

    if (screen_name && strcmp(screen_name, "about") == 0) {
        if (ui_settings == NULL) ui_settings_screen_init();
        lv_disp_load_scr(ui_settings);
        build_about_overlay();
        screen_name = NULL;   /* handled — skip the screen table lookup */
    }

    /* Apps menu populated with 17 entries to verify the dynamic grid + scroll. */
    if (screen_name && strcmp(screen_name, "apps17") == 0) {
        static AppMenuEntry_t e[17];
        static const char * nm[17] = {
            "Dino","Matrix Rain","VU Meter","Retro TV","PC Monitor","Air Mouse",
            "BLE Spam","BadUSB","Infrared","MeowGotchi","WiFi Analyzer","Firmware",
            "DeauthDetect","BLE SpamDet","Rogue Radar","ProbeSniffer","TrackerDetect" };
        const void * ic[6] = {
            &ui_img_dino_png, &ui_img_matrix_rain_png, &ui_img_vu_meter_png,
            &ui_img_retro_tv_png, &ui_img_air_mouse_png, &ui_img_badusb_png };
        for (int i = 0; i < 17; i++) {
            strncpy(e[i].name, nm[i], sizeof(e[i].name) - 1);
            e[i].icon = ic[i % 6];
        }
        ui_apps_menu_load_apps(e, 17);
        ui_apps_menu_screen_destroy();   /* drop any pre-built empty menu */
        ui_apps_menu_screen_init();      /* rebuild tiles from the entries */
        lv_disp_load_scr(ui_apps_menu);
        screen_name = NULL;
    }

    /* The tabview opens on the Display tab; jump to Time (index 3) so the
     * "Sync over WiFi" button is visible in the shot. */
    if (screen_name && strcmp(screen_name, "time_tab") == 0) {
        extern lv_obj_t * ui_tabview_settings;
        if (ui_tabview == NULL) ui_tabview_screen_init();
        lv_disp_load_scr(ui_tabview);
        lv_tabview_set_act(ui_tabview_settings, 3, LV_ANIM_OFF);
        screen_name = NULL;
    }

    if (screen_name) {
        const screen_entry_t *sel = NULL;
        for (int k = 0; k < SCREEN_COUNT; k++) {
            if (strcmp(SCREENS[k].name, screen_name) == 0) { sel = &SCREENS[k]; break; }
        }
        if (!sel) {
            fprintf(stderr, "écran inconnu : %s (voir --list)\n", screen_name);
            return 2;
        }
        if (*sel->screen == NULL) sel->init();   /* création paresseuse */
        lv_disp_load_scr(*sel->screen);
    }

    if (shot_path) {
        /* Laisse l'UI se stabiliser (thème, images, animations d'entrée). */
        for (int i = 0; i < 90; i++) { lv_timer_handler(); SDL_Delay(16); }
        int rc = save_bmp(shot_path);
        SDL_Quit();
        return rc;
    }

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (e.key.keysym.sym == SDLK_s) save_bmp("meowkit-shot.bmp");
            }
        }
        lv_timer_handler();
        present();
        SDL_Delay(5);
    }
    SDL_Quit();
    return 0;
}
