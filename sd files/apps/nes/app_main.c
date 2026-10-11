/* NES SD frontend. Menus are replaceable independently of the firmware core.
 * Firmware owns ROM files, saves, real-time input/display/audio and power.
 * This app retains no firmware pointers and never calls hardware drivers. */
#include "mk_app_abi.h"
#include "mk_nes_abi.h"

enum { LIBRARY_ROWS = 5, LIBRARY_TOP = 48, LIBRARY_ROW_HEIGHT = 28 };
static mk_nes_catalog_t catalog;
static mk_nes_entry_t page[LIBRARY_ROWS];
static uint32_t selected;
static int catalog_ok;
static int session_active;

static unsigned length(const char* text)
{
    unsigned n = 0;
    while (text[n]) ++n;
    return n;
}

static void copy(char* out, unsigned capacity, const char* text)
{
    unsigned n = 0;
    while (n + 1 < capacity && text[n]) { out[n] = text[n]; ++n; }
    out[n] = 0;
}

static void paragraph(const char* message)
{
    unsigned offset = 0;
    int y = 50;
    while (message[offset] && y < 204) {
        char line[43];
        unsigned n = 0;
        while (n < sizeof(line)-1 && message[offset+n] && message[offset+n] != '\n') {
            line[n] = message[offset+n]; ++n;
        }
        line[n] = 0;
        offset += n;
        if (message[offset] == '\n') ++offset;
        mk_gfx_text(MK_PAD, y, line, MK_COL_TEXT);
        y += 19;
    }
}

static void notice(const char* title, const char* message)
{
    int dirty = 1;
    mk_nes_wait_release();
    for (;;) {
        mk_nes_input_t input;
        if (dirty) {
            mk_gfx_clear(); mk_gfx_header(title); paragraph(message);
            mk_gfx_footer("OK", "Back"); mk_gfx_present(); dirty = 0;
        }
        if (session_active) mk_nes_poll(&input);
        else {
            /* A version/begin failure has no NES input owner yet. */
            input.pressed = 0; input.touch_pressed = 0; input.sleep_requested = 0;
            mk_input_poll();
            if (mk_btn(MK_BTN_A)) input.pressed |= MK_NES_A;
            if (mk_btn(MK_BTN_B)) input.pressed |= MK_NES_B;
        }
        if ((input.pressed & (MK_NES_A | MK_NES_B)) || input.touch_pressed) break;
        if (input.sleep_requested) {
            /* A failed sleep retains the session; the user can acknowledge the
             * original notice and retry from the pause menu. */
            mk_nes_command(MK_NES_SLEEP, 0);
            dirty = 1;
        }
        mk_delay(8);
    }
    mk_nes_wait_release();
}

static void error(const char* title)
{
    char message[160];
    mk_nes_error(message, sizeof(message));
    if (!message[0]) copy(message, sizeof(message), "The NES service could not complete this action.");
    notice(title, message);
}

static void scan(const char* path)
{
    catalog_ok = mk_nes_catalog_open(path) >= 0;
    mk_nes_catalog_info(&catalog);
    /* Failed opens retain a bounded firmware path for retry/back navigation. */
    if (!catalog.path[0]) copy(catalog.path, sizeof(catalog.path), MK_NES_ROOT);
    selected = 0;
}

static void draw_library(void)
{
    char title[48], detail[64];
    uint32_t first = (selected / LIBRARY_ROWS) * LIBRARY_ROWS;
    uint32_t pages = (catalog.count + LIBRARY_ROWS - 1) / LIBRARY_ROWS;
    mk_snprintf(title, sizeof(title), "NES  %lu/%lu (%lu%s)",
                (unsigned long)(selected / LIBRARY_ROWS + 1),
                (unsigned long)(pages ? pages : 1), (unsigned long)catalog.count,
                catalog.flags & MK_NES_LIST_TRUNCATED ? "+" : "");
    mk_gfx_clear(); mk_gfx_header(title);
    mk_snprintf(detail, sizeof(detail), "%.42s", catalog.path);
    mk_gfx_text(MK_PAD, 29, detail, MK_COL_MUTED);
    if (!catalog_ok) {
        mk_gfx_text(MK_PAD, 78, "Folder unavailable / SD removed.", MK_COL_ERR);
        mk_gfx_text(MK_PAD, 103, "A: retry folder   B: back", MK_COL_TEXT);
    } else if (!catalog.count) {
        mk_gfx_text(MK_PAD, 78, "No subfolders or .nes files here.", MK_COL_TEXT);
        mk_gfx_text(MK_PAD, 103, "Copy ROMs to /roms/nes/", MK_COL_MUTED);
        mk_gfx_text(MK_PAD, 128, "A: rescan   B: back", MK_COL_TEXT);
    } else {
        unsigned slot;
        for (slot = 0; slot < LIBRARY_ROWS && first + slot < catalog.count; ++slot) {
            int y = LIBRARY_TOP + (int)slot * LIBRARY_ROW_HEIGHT;
            int active = first + slot == selected;
            if (mk_nes_catalog_entry(first + slot, &page[slot]) != MK_NES_OK) {
                page[slot].name[0] = 0;
                page[slot].flags = 0;
            }
            mk_gfx_fill_round_rect(6, y, 308, 26, 4, active ? MK_COL_ACCENT_DARK : MK_COL_ITEM_BG);
            mk_snprintf(detail, sizeof(detail), "%s%.38s",
                        page[slot].flags & MK_NES_DIRECTORY ? "+ " : "",
                        page[slot].name[0] ? page[slot].name : "Entry unavailable");
            mk_gfx_text(12, y+5, detail, active ? MK_COL_ACCENT : MK_COL_TEXT);
        }
        {
            const mk_nes_entry_t* entry = &page[selected - first];
            if (entry->flags & MK_NES_DIRECTORY) copy(detail, sizeof(detail), "Folder - A or touch to open");
            else if (!(entry->flags & MK_NES_HEADER_VALID)) copy(detail, sizeof(detail), "Invalid or unavailable ROM header");
            else mk_snprintf(detail, sizeof(detail), "Mapper %lu | %s | %luK%s",
                            (unsigned long)entry->mapper, entry->region == 50 ? "PAL" : "NTSC",
                            (unsigned long)(entry->bytes / 1024),
                            entry->flags & MK_NES_SUPPORTED ? "" : " | unsupported");
            mk_gfx_text(MK_PAD, 190, detail, MK_COL_MUTED);
        }
    }
    if (catalog.flags & MK_NES_LIST_TRUNCATED)
        mk_gfx_text(MK_PAD, 205, "List limited; use smaller folders.", MK_COL_WARN);
    else if (catalog.flags & MK_NES_LIST_SKIPPED)
        mk_gfx_text(MK_PAD, 205, "Long/invalid names skipped.", MK_COL_WARN);
    mk_gfx_footer("A: open  L/R: page", length(catalog.path) > length(MK_NES_ROOT) ? "B: back" : "B: exit");
    mk_gfx_present();
}

/* Returns 1 to continue gameplay, 0 after a successful close. A failed close
 * deliberately stays in this menu so a save can be retried without losing RAM. */
static int pause_menu(void)
{
    unsigned choice = 0;
    int dirty = 1;
    mk_nes_state_t state;
    mk_nes_wait_release();
    for (;;) {
        mk_nes_input_t input;
        mk_nes_state(&state);
        if (!state.loaded) return 0;
        if (dirty) {
            const char* labels[6];
            unsigned i;
            labels[0] = "Resume"; labels[1] = "Save cartridge SRAM";
            labels[2] = "Reset game"; labels[3] = state.sound ? "Sound: on" : "Sound: off";
            labels[4] = "Sleep"; labels[5] = "Exit to library";
            mk_gfx_clear(); mk_gfx_header("NES paused");
            for (i = 0; i < 6; ++i) {
                int y = 32 + (int)i * 29;
                mk_gfx_fill_round_rect(6, y, 308, 27, 4, i == choice ? MK_COL_ACCENT_DARK : MK_COL_ITEM_BG);
                mk_gfx_text(12, y+5, labels[i], i == choice ? MK_COL_ACCENT : MK_COL_TEXT);
            }
            mk_gfx_footer("A: select", "B: resume"); mk_gfx_present(); dirty = 0;
        }
        mk_nes_poll(&input);
        if (input.sleep_requested) {
            if (mk_nes_command(MK_NES_SLEEP, 0) != MK_NES_OK) { error("Sleep cancelled"); dirty = 1; }
            else { mk_nes_wait_release(); return 1; }
        }
        if (input.pressed & MK_NES_B) { mk_nes_wait_release(); return 1; }
        if (input.pressed & MK_NES_UP) { choice = (choice + 5) % 6; dirty = 1; }
        if (input.pressed & MK_NES_DOWN) { choice = (choice + 1) % 6; dirty = 1; }
        {
            int enter = (input.pressed & MK_NES_A) != 0;
            if (input.touch_pressed && input.touch_y >= 32 && input.touch_y < 206) {
                choice = (unsigned)(input.touch_y - 32) / 29; enter = 1;
            }
            if (enter) {
                int result = MK_NES_OK;
                if (choice == 0) { mk_nes_wait_release(); return 1; }
                if (choice == 1) {
                    result = mk_nes_command(MK_NES_SAVE, 0);
                    if (result == MK_NES_OK) notice("Cartridge SRAM", state.sram_bytes ? "SRAM saved." : "This cartridge has no battery-backed SRAM.");
                } else if (choice == 2) result = mk_nes_command(MK_NES_RESET, 0);
                else if (choice == 3) result = mk_nes_command(MK_NES_SOUND, !state.sound);
                else if (choice == 4) result = mk_nes_command(MK_NES_SLEEP, 0);
                else if (choice == 5) result = mk_nes_command(MK_NES_CLOSE, 0);
                if (result != MK_NES_OK) error("Action failed");
                else if (choice == 5) { mk_nes_wait_release(); return 0; }
                else if (choice == 2 || choice == 4) { mk_nes_wait_release(); return 1; }
                mk_nes_wait_release(); dirty = 1;
            }
        }
        mk_delay(8);
    }
}

static void play(const char* path)
{
    mk_nes_state_t state;
    mk_gfx_clear(); mk_gfx_header("Loading NES..."); mk_gfx_present();
    if (mk_nes_open(path) != MK_NES_OK) { error("Cannot load ROM"); return; }
    mk_nes_state(&state);
    if (state.flags & MK_NES_EXPERIMENTAL)
        notice("Experimental mapper", "This cartridge uses incomplete mapper emulation. Graphics, sound or gameplay may differ from a real NES.");
    for (;;) {
        int result = mk_nes_run();
        if (result == MK_NES_SLEEP_REQUESTED) {
            if (mk_nes_command(MK_NES_SLEEP, 0) == MK_NES_OK) continue;
            error("Sleep cancelled");
        } else if (result < 0) error("Emulation stopped");
        if (!pause_menu()) break;
    }
}

int app_main(int argc, char** argv)
{
    int dirty = 1;
    (void)argc; (void)argv;
    session_active = 0;
    if (mk_nes_version() != MK_NES_ABI_VERSION) {
        notice("NES update required", "Install firmware with NES service API 1 to run this SD app.");
        return 1;
    }
    if (mk_nes_begin() != MK_NES_OK) {
        char message[160];
        mk_nes_error(message, sizeof(message));
        mk_nes_end();
        notice("NES unavailable", message[0] ? message : "The NES service could not start.");
        return 1;
    }
    session_active = 1;
    scan(MK_NES_ROOT); mk_nes_wait_release();
    for (;;) {
        mk_nes_input_t input;
        if (dirty) { draw_library(); dirty = 0; }
        mk_nes_poll(&input);
        if (input.sleep_requested) {
            if (mk_nes_command(MK_NES_SLEEP, 0) != MK_NES_OK) error("Sleep cancelled");
            dirty = 1; mk_nes_wait_release(); continue;
        }
        if (input.pressed & MK_NES_B) {
            char parent[MK_NES_PATH_BYTES];
            unsigned n = length(catalog.path), root = length(MK_NES_ROOT);
            if (n <= root) break;
            copy(parent, sizeof(parent), catalog.path);
            while (n > root && parent[n-1] != '/') --n;
            parent[n > root ? n-1 : root] = 0;
            scan(parent); mk_nes_wait_release(); dirty = 1; continue;
        }
        if (catalog_ok && catalog.count) {
            if (input.pressed & MK_NES_UP) { selected = (selected + catalog.count - 1) % catalog.count; dirty = 1; }
            if (input.pressed & MK_NES_DOWN) { selected = (selected + 1) % catalog.count; dirty = 1; }
            if (input.pressed & MK_NES_LEFT) { selected = selected >= LIBRARY_ROWS ? selected - LIBRARY_ROWS : 0; dirty = 1; }
            if (input.pressed & MK_NES_RIGHT) {
                selected += LIBRARY_ROWS;
                if (selected >= catalog.count) selected = catalog.count - 1;
                dirty = 1;
            }
        }
        {
            int enter = (input.pressed & MK_NES_A) != 0;
            if (input.touch_pressed && input.touch_y >= LIBRARY_TOP &&
                input.touch_y < LIBRARY_TOP + LIBRARY_ROWS * LIBRARY_ROW_HEIGHT && catalog_ok) {
                uint32_t tapped = selected / LIBRARY_ROWS * LIBRARY_ROWS +
                                  (uint32_t)(input.touch_y - LIBRARY_TOP) / LIBRARY_ROW_HEIGHT;
                if (tapped < catalog.count) { selected = tapped; enter = 1; }
            }
            if (enter) {
                if (!catalog_ok || !catalog.count) {
                    char path[MK_NES_PATH_BYTES]; copy(path, sizeof(path), catalog.path); scan(path);
                } else {
                    mk_nes_entry_t entry;
                    if (mk_nes_catalog_entry(selected, &entry) != MK_NES_OK) error("Entry unavailable");
                    else {
                        char path[MK_NES_PATH_BYTES];
                        int n = mk_snprintf(path, sizeof(path), "%s/%s", catalog.path, entry.name);
                        if (n < 0 || (unsigned)n >= sizeof(path)) notice("Path too long", "ROM paths must fit in 255 bytes.");
                        else if (entry.flags & MK_NES_DIRECTORY) scan(path);
                        else play(path);
                    }
                }
                mk_nes_wait_release(); dirty = 1;
            }
        }
        mk_delay(8);
    }
    mk_nes_end();
    session_active = 0;
    return 0;
}
