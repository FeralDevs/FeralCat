/**
 * @file  app_17.cpp
 * @brief Tracker Detector app — see app_17.h.
 */
#include "app_17.h"
#include <Arduino.h>
#include "../app_common/mk_tui.h"
#include "../../system/screenshot.h"

namespace MOONCAKE::APPS
{

App17::App17(DEVICES* device) : _device(device)
{
    setAppInfo().name = "TrackerDetect";
}

template<typename LCD>
void App17::_render(LCD& lcd)
{
    const TrackerStats& s = _mon.stats();
    int n = _mon.trackers(_trkbuf, TrackerMonitor::MAXTRK);

    TrackerUI::View v;
    v.nearby     = s.nearby;
    v.persistent = s.persistent;
    v.alert      = s.alert;
    v.running    = _mon.running();
    v.now_ms     = millis();
    TrackerUI::drawList(lcd, v, _trkbuf, n);
}

void App17::_present()
{
    if (_haveCanvas) {
        _render(*_canvas);
        _canvas->pushSprite(&_device->Lcd, 0, 0);
    } else {
        _render(_device->Lcd);
    }
}

void App17::onOpen()
{
    _haveCanvas = false;
    _canvas = new lgfx::LGFX_Sprite(&_device->Lcd);
    if (_canvas) {
        _canvas->setColorDepth(16);
        _canvas->setPsram(true);
        _haveCanvas = _canvas->createSprite(MK_LAYOUT::W, MK_LAYOUT::H);
    }

    _mon.begin();
    _dirty = true;
}

void App17::onRunning()
{
    _device->button.update();
    _device->button.tick();

    _mon.loop();

    if (_device->button.A.pressed()) {   /* short B = exit is launcher-handled */
        if (_mon.running()) _mon.pause(); else _mon.resume();
        _dirty = true;
    }

    const uint32_t now = millis();
    if (_dirty || now - _lastDraw >= 500) { _present(); _lastDraw = now; _dirty = false; }

    if (_haveCanvas) screenshot_tui_tick(_device, _canvas);   /* Up+Down = save to SD */
    delay(20);
}

void App17::onClose()
{
    _mon.stop();
    if (_canvas) { _canvas->deleteSprite(); delete _canvas; _canvas = nullptr; }
    _haveCanvas = false;
}

} // namespace MOONCAKE::APPS
