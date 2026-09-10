/**
 * @file  app_15.cpp
 * @brief Rogue Radar app — see app_15.h.
 */
#include "app_15.h"
#include <Arduino.h>
#include <algorithm>
#include <cstring>
#include "../app_common/mk_tui.h"

namespace MOONCAKE::APPS
{

App15::App15(DEVICES* device) : _device(device)
{
    setAppInfo().name = "RogueRadar";
}

void App15::_scan()
{
    _scanning = true;
    _present();                         /* "Scanning..." before we block */

    static WiFiAPInfo tmp[MAXTW];
    int n = _device->wifi.scan(tmp, MAXTW);
    if (n < 0) n = 0;

    _tw_n = 0;
    for (int i = 0; i < n; i++) {
        RogueUI::TwinRow* row = nullptr;
        for (int j = 0; j < _tw_n; j++)
            if (strcmp(_tw[j].ssid, tmp[i].ssid) == 0) { row = &_tw[j]; break; }
        if (!row) {
            if (_tw_n >= MAXTW) continue;
            row = &_tw[_tw_n++];
            strncpy(row->ssid, tmp[i].ssid, sizeof(row->ssid) - 1);
            row->ssid[sizeof(row->ssid) - 1] = '\0';
            row->bssids = 0; row->open = false; row->secure = false;
        }
        row->bssids++;
        if (tmp[i].encrypted) row->secure = true; else row->open = true;
    }

    /* Twins (open+secure same SSID) first, then by BSSID count. */
    std::sort(_tw, _tw + _tw_n, [](const RogueUI::TwinRow& a, const RogueUI::TwinRow& b) {
        bool ta = RogueUI::isTwin(a), tb = RogueUI::isTwin(b);
        if (ta != tb) return ta;
        return a.bssids > b.bssids;
    });
    _flagged = 0;
    for (int i = 0; i < _tw_n; i++) if (RogueUI::isTwin(_tw[i])) _flagged++;

    _sel = 0; _scroll = 0;
    _scanning = false;
    _dirty = true;
}

template<typename LCD>
void App15::_render(LCD& lcd)
{
    if (_mode == Mode::Twins) {
        RogueUI::drawTwins(lcd, _tw, _tw_n, _sel, _scroll, _scanning, _flagged);
    } else {
        _flood.history(_histbuf);
        RogueUI::FloodView v;
        v.s        = _flood.stats();
        v.running  = _flood.running();
        v.hist     = _histbuf;
        v.histLen  = BeaconFlood::HIST;
        v.threshold = BeaconFlood::ALERT_UNIQ;
        RogueUI::drawFlood(lcd, v);
    }
}

void App15::_present()
{
    if (_haveCanvas) {
        _render(*_canvas);
        _canvas->pushSprite(&_device->Lcd, 0, 0);
    } else {
        _render(_device->Lcd);
    }
}

void App15::onOpen()
{
    _device->wifi.begin();

    _haveCanvas = false;
    _canvas = new lgfx::LGFX_Sprite(&_device->Lcd);
    if (_canvas) {
        _canvas->setColorDepth(16);
        _canvas->setPsram(true);
        _haveCanvas = _canvas->createSprite(MK_LAYOUT::W, MK_LAYOUT::H);
    }

    _mode = Mode::Twins;
    _scan();
}

void App15::onRunning()
{
    _device->button.update();
    _device->button.tick();

    const int VIS = MK_LAYOUT::CONTENT_ROWS;

    if (_mode == Mode::Twins) {
        if (_device->button.Up.pressed() && _sel > 0) {
            _sel--; if (_sel < _scroll) _scroll = _sel; _dirty = true;
        }
        if (_device->button.Down.pressed() && _sel < _tw_n - 1) {
            _sel++; if (_sel >= _scroll + VIS) _scroll = _sel - VIS + 1; _dirty = true;
        }
        if (_device->button.A.pressed()) { _scan(); }
        if (_device->button.B.pressed()) {         /* short B → Flood view */
            _mode = Mode::Flood; _flood.begin(); _dirty = true;
        }
        if (_dirty) { _present(); _dirty = false; }
    }
    else { /* Mode::Flood */
        _flood.loop();
        if (_device->button.A.pressed()) {
            if (_flood.running()) _flood.pause(); else _flood.resume();
            _dirty = true;
        }
        if (_device->button.B.pressed()) {         /* short B → Twins view */
            _flood.stop();
            _mode = Mode::Twins;
            _scan();
        }
        const uint32_t now = millis();
        if (_dirty || now - _lastDraw >= 500) { _present(); _lastDraw = now; _dirty = false; }
    }

    delay(20);
}

void App15::onClose()
{
    _flood.stop();
    if (_canvas) { _canvas->deleteSprite(); delete _canvas; _canvas = nullptr; }
    _haveCanvas = false;
}

} // namespace MOONCAKE::APPS
