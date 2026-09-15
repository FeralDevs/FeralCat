/*
 * MeowPlayer — native MeowKit app (signed ELF, app SDK).
 *
 * A thin front-end over the firmware audio engine (ES8311 + Helix MP3 decoder
 * on a core-0 worker). Scans /music (.mp3), shows now-playing + a Songs list,
 * and toggles Speaker/Jack output. Engine stays in firmware; this app just
 * sends commands and renders the status snapshot.
 *
 * Controls (now): A play/pause · Up/Down volume · Left/Right seek ·
 *                 tap B menu · hold B exit
 *          (menu): Up/Down move · A select · B back
 *          (songs): Up/Down move · A play · B back
 */
#include "mk_app_abi.h"

#define MAXT 64

static mk_track_t g_tracks[MAXT];
static int        g_ntracks = 0;
static int        g_cur     = -1;
static uint32_t   g_cachedGen = 0xFFFFFFFFu;
static uint32_t   g_lastFinished = 0;
static int        g_ampOn   = 1;   /* 1 = speaker, 0 = jack */
static char       g_menu[2][40];

enum { NOW = 0, MENU, SONGS };

static int streq(const char* a, const char* b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void build_menu(void)
{
    mk_snprintf(g_menu[0], sizeof(g_menu[0]), "Songs (%d)", g_ntracks);
    mk_snprintf(g_menu[1], sizeof(g_menu[1]), "Output: %s", g_ampOn ? "Speaker" : "Jack");
}

static void fmt_time(char* b, int n, int sec)
{
    if (sec < 0) sec = 0;
    mk_snprintf(b, n, "%d:%02d", sec / 60, sec % 60);
}

/* small speaker glyph centered at (cx,cy) */
static void draw_speaker(int cx, int cy, uint32_t col)
{
    mk_gfx_fill_rect(cx - 8, cy - 3, 4, 6, col);
    mk_gfx_fill_triangle(cx - 4, cy - 7, cx - 4, cy + 7, cx + 5, cy, col);
}

static void refresh_catalog(void)
{
    g_ntracks = 0;
    while (g_ntracks < MAXT) {
        int n = mk_media_tracks(g_ntracks, &g_tracks[g_ntracks], MAXT - g_ntracks);
        if (n <= 0) break;
        g_ntracks += n;
        if (n < 8) break;   /* a short page = end of catalog */
    }
}

static void play_idx(int idx)
{
    if (idx < 0 || idx >= g_ntracks) return;
    mk_media_cmd(MK_MEDIA_PLAY, g_tracks[idx].id);
    g_cur = idx;
}

/* ── Now-Playing ── */
static void draw_now(const mk_media_status_t* s, int ready)
{
    mk_gfx_clear();
    mk_gfx_header("MeowPlayer");
    if (!ready) {
        mk_gfx_text(MK_PAD, 90, "Audio init failed (ES8311)", MK_COL_ERR);
        mk_gfx_footer("", "Exit");
        mk_gfx_present();
        return;
    }

    int playing = (streq(s->state, "playing"));
    const char* title = s->title[0] ? s->title
                      : (streq(s->state, "scanning") ? "Scanning /music..." : "(no track)");

    draw_speaker(MK_SCREEN_W - 20, 38, MK_COL_MUTED);
    mk_gfx_text(MK_PAD, 32, title, MK_COL_WHITE);
    mk_gfx_text(MK_PAD, 52, g_ampOn ? "Speaker" : "Headphones (jack)", MK_COL_MUTED);

    /* play/pause disc */
    int cx = MK_SCREEN_W / 2, cy = 104, r = 26;
    mk_gfx_circle(cx, cy, r, MK_COL_ACCENT);
    mk_gfx_circle(cx, cy, r - 1, MK_COL_ACCENT);
    if (playing) {
        mk_gfx_fill_rect(cx - 8, cy - 11, 5, 22, MK_COL_ACCENT);
        mk_gfx_fill_rect(cx + 3, cy - 11, 5, 22, MK_COL_ACCENT);
    } else {
        mk_gfx_fill_triangle(cx - 7, cy - 12, cx - 7, cy + 12, cx + 12, cy, MK_COL_ACCENT);
    }

    /* progress bar */
    int py = 150, bx = MK_PAD, bw = MK_SCREEN_W - bx - 16;
    mk_gfx_fill_round_rect(bx, py - 3, bw, 6, 3, MK_COL_ITEM_BG);
    int fw = (s->duration > 0) ? (int)((long)bw * s->position / s->duration) : 0;
    if (fw > 0) mk_gfx_fill_round_rect(bx, py - 3, fw, 6, 3, MK_COL_ACCENT);
    mk_gfx_fill_circle(bx + fw, py, 5, MK_COL_WHITE);
    char t0[8], t1[8], line[24];
    fmt_time(t0, sizeof(t0), (int)s->position);
    fmt_time(t1, sizeof(t1), (int)s->duration);
    mk_snprintf(line, sizeof(line), "%s / %s", t0, t1);
    mk_gfx_text(bx, py + 10, line, MK_COL_MUTED);

    /* volume slider */
    int vy = 178, vx = 74, vw = MK_SCREEN_W - 74 - 40;
    draw_speaker(MK_PAD + 8, vy + 3, MK_COL_MUTED);
    mk_gfx_fill_round_rect(vx, vy, vw, 6, 3, MK_COL_ITEM_BG);
    int vf = vw * s->volume / 100;
    if (vf > 0) mk_gfx_fill_round_rect(vx, vy, vf, 6, 3, MK_COL_ACCENT);
    mk_gfx_fill_circle(vx + vf, vy + 3, 6, MK_COL_WHITE);
    char vb[6]; mk_snprintf(vb, sizeof(vb), "%d", s->volume);
    mk_gfx_text(vx + vw + 8, vy + 12, vb, MK_COL_MUTED);

    mk_gfx_footer(playing ? "Pause" : "Play", "Menu");
    mk_gfx_present();
}

/* ── Menu / Songs list ── */
static void draw_list(const char* title, int count, int sel, int top, int is_menu)
{
    mk_gfx_clear();
    mk_gfx_header(title);
    int y = MK_CONTENT_Y + 6;
    if (count == 0 && !is_menu) {
        mk_gfx_text(MK_PAD, y + 20, "No music in /music (.mp3)", MK_COL_MUTED);
    }
    char row[48];
    for (int r = 0; r < 8; r++) {
        int i = top + r;
        if (i >= count) break;
        int selr = (i == sel);
        if (selr) mk_gfx_fill_round_rect(4, y - 2, MK_SCREEN_W - 8, 20, 3, MK_COL_ITEM_BG);
        const char* label = is_menu ? g_menu[i] : g_tracks[i].title;
        mk_snprintf(row, sizeof(row), "%s %.34s", selr ? ">" : " ", label);
        mk_gfx_text(MK_PAD, y, row, selr ? MK_COL_ACCENT : MK_COL_WHITE);
        y += 22;
    }
    mk_gfx_footer("Select", "Back");
    mk_gfx_present();
}

int app_main(int argc, char** argv)
{
    (void)argc; (void)argv;
    int ready = mk_media_begin();
    g_ampOn = 1;

    int screen = NOW, msel = 0, ssel = 0, stop = 0, dirty = 1;
    uint32_t last_poll = 0, last_draw = 0;
    mk_media_status_t st; mk_media_status(&st);

    for (;;) {
        mk_input_poll();
        uint32_t now = mk_millis();

        /* poll status ~4 Hz */
        if (ready && now - last_poll >= 250) {
            last_poll = now;
            mk_media_status(&st);
            if (st.generation != g_cachedGen && !streq(st.state, "scanning")) {
                refresh_catalog(); g_cachedGen = st.generation;
            }
            if (st.finished != g_lastFinished) {
                g_lastFinished = st.finished;
                if (g_cur + 1 < g_ntracks) play_idx(g_cur + 1);
            }
            dirty = 1;
        }

        if (mk_btn_long(MK_BTN_B)) break;   /* hold B → exit */

        int a  = mk_btn(MK_BTN_A),  b  = mk_btn(MK_BTN_B);
        int up = mk_btn(MK_BTN_UP), dn = mk_btn(MK_BTN_DOWN);
        int lf = mk_btn(MK_BTN_LEFT), rt = mk_btn(MK_BTN_RIGHT);
        if (a||b||up||dn||lf||rt) dirty = 1;

        if (screen == NOW) {
            if (a)  mk_media_cmd(MK_MEDIA_PAUSE, 0);
            if (b)  { msel = 0; screen = MENU; }
            if (up) { int v=st.volume+5; if(v>100)v=100; mk_media_cmd(MK_MEDIA_VOLUME,v); st.volume=v; }
            if (dn) { int v=(int)st.volume-5; if(v<0)v=0; mk_media_cmd(MK_MEDIA_VOLUME,v); st.volume=v; }
            if (lf) mk_media_cmd(MK_MEDIA_SEEK, (int)st.position > 5 ? (int)st.position - 5 : 0);
            if (rt) mk_media_cmd(MK_MEDIA_SEEK, (int)st.position + 5);
        } else if (screen == MENU) {
            if (up && msel > 0) msel--;
            if (dn && msel < 1) msel++;
            if (a) {
                if (msel == 0) { ssel = (g_cur >= 0) ? g_cur : 0; stop = 0; screen = SONGS; }
                else           { g_ampOn = !g_ampOn; mk_media_set_output(g_ampOn); }
            }
            if (b) screen = NOW;
        } else { /* SONGS */
            if (up && ssel > 0) ssel--;
            if (dn && ssel < g_ntracks - 1) ssel++;
            if (ssel < stop) stop = ssel;
            if (ssel > stop + 7) stop = ssel - 7;
            if (a && g_ntracks > 0) { play_idx(ssel); screen = NOW; }
            if (b) screen = NOW;
        }

        if (dirty || now - last_draw >= 300) {
            if (screen == NOW)        draw_now(&st, ready);
            else if (screen == MENU)  { build_menu(); draw_list("Menu", 2, msel, 0, 1); }
            else                      draw_list("Songs", g_ntracks, ssel, stop, 0);
            last_draw = now; dirty = 0;
        }
        mk_delay(20);
    }

    mk_media_end();
    return 0;
}
