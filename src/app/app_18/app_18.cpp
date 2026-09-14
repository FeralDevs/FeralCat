/**
 * @file  app_18.cpp
 * @brief Script Runner app — see app_18.h.
 */
#include "app_18.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include "../app_common/mk_tui.h"
#include "../../system/berry_engine.h"

namespace MOONCAKE::APPS
{

/* ── Console line buffer (fed by print() via the Berry output sink) ──────── */
namespace {
constexpr int LOG_LINES = 11;
constexpr int LOG_COLS  = 40;
char s_log[LOG_LINES][LOG_COLS];
int  s_count = 0;   /* lines in use */
int  s_col   = 0;   /* column in the current (last) line */

void log_reset() { s_count = 1; s_col = 0; s_log[0][0] = '\0'; }

void log_newline()
{
    if (s_count < LOG_LINES) {
        s_count++;
    } else {                                   /* scroll up */
        for (int i = 0; i < LOG_LINES - 1; i++) memcpy(s_log[i], s_log[i + 1], LOG_COLS);
    }
    s_col = 0;
    s_log[s_count - 1][0] = '\0';
}

void log_putc(char c)
{
    if (s_count == 0) log_reset();
    if (c == '\r') return;
    if (c == '\n') { log_newline(); return; }
    if (s_col >= LOG_COLS - 1) log_newline();  /* wrap long lines */
    s_log[s_count - 1][s_col++] = c;
    s_log[s_count - 1][s_col] = '\0';
}

void log_sink(const char* t, int n) { for (int i = 0; i < n; i++) log_putc(t[i]); }
} // namespace

App18::App18(DEVICES* device) : _device(device)
{
    setAppInfo().name = "ScriptRunner";
}

/* Robust SD probe (launcher mounts the global SD_MMC directly). */
static bool sd_ok()
{
    File r = SD_MMC.open("/");
    if (r) { r.close(); return true; }
    SD_MMC.end();
    return SD_MMC.begin("/sdcard", true, false, 10000);
}

void App18::_scan()
{
    _nfiles = 0;
    _sel = 0;
    if (!sd_ok()) return;
    if (!SD_MMC.exists("/scripts")) { SD_MMC.mkdir("/scripts"); return; }

    File dir = SD_MMC.open("/scripts");
    if (!dir || !dir.isDirectory()) return;
    for (File f = dir.openNextFile(); f && _nfiles < MAXF; f = dir.openNextFile()) {
        if (!f.isDirectory()) {
            const char* nm = f.name();
            const char* base = strrchr(nm, '/');
            base = base ? base + 1 : nm;
            int len = strlen(base);
            if (len > 3 && strcasecmp(base + len - 3, ".be") == 0) {
                strncpy(_files[_nfiles], base, sizeof(_files[0]) - 1);
                _files[_nfiles][sizeof(_files[0]) - 1] = '\0';
                _nfiles++;
            }
        }
        f.close();
    }
    dir.close();
}

void App18::_runSelected()
{
    if (_sel < 0 || _sel >= _nfiles) return;
    snprintf(_title, sizeof(_title), "%s", _files[_sel]);

    char path[64];
    snprintf(path, sizeof(path), "/scripts/%s", _files[_sel]);
    File f = SD_MMC.open(path, FILE_READ);
    log_reset();
    if (!f || f.isDirectory()) { if (f) f.close(); log_sink("cannot open file\n", 17); _mode = Mode::Console; return; }

    size_t sz = f.size();
    char* code = (char*)malloc(sz + 1);
    if (!code) { f.close(); log_sink("out of memory\n", 14); _mode = Mode::Console; return; }
    f.read((uint8_t*)code, sz);
    code[sz] = '\0';
    f.close();

    _mode = Mode::Console;
    _present();                 /* show a blank console before the (blocking) run */

    berry_set_output(log_sink);
    char err[96] = {0};
    int rc = berry_run(code, 20000 /* ms */, err, sizeof(err));
    berry_set_output(nullptr);
    free(code);

    log_putc('\n');
    if (rc == 0) log_sink("-- done --\n", 11);
    else { log_sink("error: ", 7); log_sink(err, strlen(err)); log_putc('\n'); }
}

template<typename LCD>
void App18::_render(LCD& lcd)
{
    MK_TUI::clearScreen(lcd);
    if (_mode == Mode::List) {
        MK_TUI::drawHeader(lcd, "Scripts");
        if (_nfiles == 0) {
            lcd.setFont(&fonts::efontCN_16);
            lcd.setTextColor((uint32_t)MK_PAL::TEXT_SEC, (uint32_t)MK_PAL::BLACK);
            lcd.setCursor(MK_LAYOUT::PAD, 100);
            lcd.printf("No .be scripts found.");
            lcd.setCursor(MK_LAYOUT::PAD, 122);
            lcd.printf("Put them in /scripts on the SD.");
            MK_TUI::drawFooter(lcd, "Rescan", "Exit");
            return;
        }
        int y = MK_LAYOUT::CONTENT_Y + 6;
        const int rows = 7;
        int first = (_sel >= rows) ? _sel - rows + 1 : 0;
        for (int i = first; i < _nfiles && i < first + rows; i++) {
            bool sel = (i == _sel);
            lcd.setFont(&fonts::efontCN_16);
            lcd.setTextColor(sel ? (uint32_t)MK_PAL::ACCENT : (uint32_t)MK_PAL::TEXT_PRI, (uint32_t)MK_PAL::BLACK);
            lcd.setCursor(MK_LAYOUT::PAD, y);
            lcd.printf("%s %s", sel ? ">" : " ", _files[i]);
            y += 22;
        }
        MK_TUI::drawFooter(lcd, "Run", "Exit");
    } else {
        MK_TUI::drawHeader(lcd, _title);
        lcd.setFont(&fonts::efontCN_16);
        int y = MK_LAYOUT::CONTENT_Y + 4;
        for (int i = 0; i < s_count; i++) {
            lcd.setTextColor((uint32_t)MK_PAL::TEXT_PRI, (uint32_t)MK_PAL::BLACK);
            lcd.setCursor(MK_LAYOUT::PAD, y);
            lcd.printf("%s", s_log[i]);
            y += 16;
        }
        MK_TUI::drawFooter(lcd, "Back", "Exit");
    }
}

void App18::_present()
{
    if (_haveCanvas) { _render(*_canvas); _canvas->pushSprite(&_device->Lcd, 0, 0); }
    else             { _render(_device->Lcd); }
}

void App18::onOpen()
{
    berry_engine_attach(_device);

    _haveCanvas = false;
    _canvas = new lgfx::LGFX_Sprite(&_device->Lcd);
    if (_canvas) {
        _canvas->setColorDepth(16);
        _canvas->setPsram(true);
        _haveCanvas = _canvas->createSprite(MK_LAYOUT::W, MK_LAYOUT::H);
    }

    _mode = Mode::List;
    _scan();
    _dirty = true;
}

void App18::onRunning()
{
    _device->button.update();
    _device->button.tick();

    if (_mode == Mode::List) {
        if (_device->button.Up.pressed())   { if (_nfiles) { _sel = (_sel + _nfiles - 1) % _nfiles; _dirty = true; } }
        if (_device->button.Down.pressed()) { if (_nfiles) { _sel = (_sel + 1) % _nfiles; _dirty = true; } }
        if (_device->button.A.pressed()) {
            if (_nfiles) { _runSelected(); _dirty = true; }
            else         { _scan(); _dirty = true; }   /* rescan */
        }
    } else {   /* Console */
        if (_device->button.A.pressed()) { _mode = Mode::List; _dirty = true; }
    }

    if (_dirty) { _present(); _dirty = false; }
    delay(20);
}

void App18::onClose()
{
    berry_set_output(nullptr);
    if (_canvas) { _canvas->deleteSprite(); delete _canvas; _canvas = nullptr; }
    _haveCanvas = false;
}

} // namespace MOONCAKE::APPS
