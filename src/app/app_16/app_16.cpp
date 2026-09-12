/**
 * @file  app_16.cpp
 * @brief Probe Sniffer app — see app_16.h.
 */
#include "app_16.h"
#include <Arduino.h>
#include "../app_common/mk_tui.h"
#include "../../system/screenshot.h"

namespace MOONCAKE::APPS
{

App16::App16(DEVICES* device) : _device(device)
{
    setAppInfo().name = "ProbeSniffer";
}

template<typename LCD>
void App16::_render(LCD& lcd)
{
    const ProbeStats& s = _mon.stats();
    _mon.history(_histbuf);

    ProbeUI::View v;
    v.channel = s.channel;
    v.total   = s.total;
    v.rate    = s.rate;
    v.peak    = s.peak;
    v.devices = s.devices;
    v.running = _mon.running();
    v.hist    = _histbuf;
    v.histLen = ProbeMonitor::HIST;

    if (_graphView) {
        ProbeUI::drawGraph(lcd, v);
    } else {
        int n = _mon.devices(_devbuf, ProbeMonitor::MAXDEV);
        ProbeUI::drawList(lcd, v, _devbuf, n);
    }
}

void App16::_present()
{
    if (_haveCanvas) {
        _render(*_canvas);
        _canvas->pushSprite(&_device->Lcd, 0, 0);
    } else {
        _render(_device->Lcd);
    }
}

void App16::onOpen()
{
    _device->wifi.begin();

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

void App16::onRunning()
{
    _device->button.update();
    _device->button.tick();

    _mon.loop();

    if (_device->button.A.pressed()) {
        if (_mon.running()) _mon.pause(); else _mon.resume();
        _dirty = true;
    }
    if (_device->button.B.pressed()) {   /* short B = list/graph; hold B = exit */
        _graphView = !_graphView; _dirty = true;
    }

    const uint32_t now = millis();
    if (_dirty || now - _lastDraw >= 500) { _present(); _lastDraw = now; _dirty = false; }

    if (_haveCanvas) screenshot_tui_tick(_device, _canvas);   /* Up+Down = save to SD */
    delay(20);
}

void App16::onClose()
{
    _mon.stop();
    if (_canvas) { _canvas->deleteSprite(); delete _canvas; _canvas = nullptr; }
    _haveCanvas = false;
}

} // namespace MOONCAKE::APPS
