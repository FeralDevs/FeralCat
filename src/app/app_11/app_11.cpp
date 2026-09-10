/**
 * @file  app_11.cpp
 * @brief WiFi Analyzer app — see app_11.h.
 */
#include "app_11.h"
#include <Arduino.h>
#include <algorithm>
#include "../app_common/mk_tui.h"

namespace MOONCAKE::APPS
{

App11::App11(DEVICES* device) : _device(device)
{
    setAppInfo().name = "WiFiAnalyzer";
}

void App11::_scan()
{
    _scanning = true;
    _present();                         /* show "Scanning..." before we block */

    static WiFiAPInfo tmp[MAXROWS];
    int n = _device->wifi.scan(tmp, MAXROWS);
    if (n < 0) n = 0;

    for (int i = 0; i < n; i++) {
        WifiAnalyzer::Row& r = _rows[i];
        strncpy(r.ssid, tmp[i].ssid, sizeof(r.ssid) - 1);
        r.ssid[sizeof(r.ssid) - 1] = '\0';
        r.rssi      = tmp[i].rssi;
        r.channel   = tmp[i].channel;
        r.encrypted = tmp[i].encrypted;
        r.auth      = tmp[i].auth;
        memcpy(r.bssid, tmp[i].bssid, 6);
    }
    std::sort(_rows, _rows + n, [](const WifiAnalyzer::Row& a, const WifiAnalyzer::Row& b) {
        return a.rssi > b.rssi;
    });

    _count    = n;
    _sel      = 0;
    _scroll   = 0;
    _scanning = false;
    _dirty    = true;
}

template<typename LCD>
void App11::_render(LCD& lcd)
{
    if (_page == Page::List)
        WifiAnalyzer::drawList(lcd, _rows, _count, _sel, _scroll, _scanning);
    else
        WifiAnalyzer::drawDetail(lcd, _rows[_sel]);
}

void App11::_present()
{
    if (_haveCanvas) {
        _render(*_canvas);
        _canvas->pushSprite(&_device->Lcd, 0, 0);
    } else {
        _render(_device->Lcd);
    }
}

void App11::onOpen()
{
    _device->wifi.begin();

    _haveCanvas = false;
    _canvas = new lgfx::LGFX_Sprite(&_device->Lcd);
    if (_canvas) {
        _canvas->setColorDepth(16);
        _canvas->setPsram(true);
        _haveCanvas = _canvas->createSprite(MK_LAYOUT::W, MK_LAYOUT::H);
    }

    _page = Page::List;
    _scan();
}

void App11::onRunning()
{
    _device->button.update();
    _device->button.tick();

    const int VIS = MK_LAYOUT::CONTENT_ROWS;

    if (_page == Page::List) {
        if (_device->button.Up.pressed() && _sel > 0) {
            _sel--;
            if (_sel < _scroll) _scroll = _sel;
            _dirty = true;
        }
        if (_device->button.Down.pressed() && _sel < _count - 1) {
            _sel++;
            if (_sel >= _scroll + VIS) _scroll = _sel - VIS + 1;
            _dirty = true;
        }
        if (_device->button.A.pressed() && _count > 0) {
            _page = Page::Detail; _dirty = true;
        }
        if (_device->button.B.pressed()) {   /* short B = rescan; hold B = exit */
            _scan();
        }
    }
    else { /* Detail */
        if (_device->button.B.pressed()) { _page = Page::List; _dirty = true; }
    }

    if (_dirty) { _present(); _dirty = false; }

    delay(20);
}

void App11::onClose()
{
    if (_canvas) { _canvas->deleteSprite(); delete _canvas; _canvas = nullptr; }
    _haveCanvas = false;
    /* Launcher repaints the menu on exit. */
}

} // namespace MOONCAKE::APPS
