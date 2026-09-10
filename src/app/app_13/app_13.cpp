/**
 * @file  app_13.cpp
 * @brief Deauth Detector app — see app_13.h.
 */
#include "app_13.h"
#include <Arduino.h>
#include "../app_common/mk_tui.h"

namespace MOONCAKE::APPS
{

App13::App13(DEVICES* device) : _device(device)
{
    setAppInfo().name = "DeauthDetect";
}

template<typename LCD>
void App13::_render(LCD& lcd)
{
    if (_logView) {
        int n = _mon.attackers(_atkbuf, 16);
        DeauthUI::drawLog(lcd, _atkbuf, n, _mon.running());
        return;
    }

    const DeauthStats& s = _mon.stats();
    _mon.history(_histbuf);

    DeauthUI::View v;
    v.channel   = s.channel;
    v.total     = s.total;
    v.rate      = s.rate;
    v.peak      = s.peak;
    v.alert     = s.alert;
    v.running   = _mon.running();
    v.uptime_s  = _mon.uptime_s();
    v.hist      = _histbuf;
    v.histLen   = DeauthMonitor::HIST;
    v.threshold = DeauthMonitor::ALERT_THRESHOLD;
    DeauthUI::draw(lcd, v);
}

void App13::_present()
{
    if (_haveCanvas) {
        _render(*_canvas);
        _canvas->pushSprite(&_device->Lcd, 0, 0);
    } else {
        _render(_device->Lcd);
    }
}

void App13::onOpen()
{
    _device->wifi.begin();

    _haveCanvas = false;
    _canvas = new lgfx::LGFX_Sprite(&_device->Lcd);
    if (_canvas) {
        _canvas->setColorDepth(16);
        _canvas->setPsram(true);
        _haveCanvas = _canvas->createSprite(MK_LAYOUT::W, MK_LAYOUT::H);
    }

    _mon.begin();          /* a monitor: starts watching immediately */
    _dirty = true;
}

void App13::onRunning()
{
    _device->button.update();
    _device->button.tick();

    _mon.loop();

    if (_device->button.A.pressed()) {
        if (_mon.running()) _mon.pause(); else _mon.resume();
        _dirty = true;
    }
    if (_device->button.B.pressed()) {   /* short B = toggle graph/log; hold B = exit */
        _logView = !_logView; _dirty = true;
    }

    const uint32_t now = millis();
    /* Refresh ~2 Hz so the rate/graph animate; the 1 Hz engine tick feeds it. */
    if (_dirty || now - _lastDraw >= 500) { _present(); _lastDraw = now; _dirty = false; }

    delay(20);
}

void App13::onClose()
{
    _mon.stop();
    if (_canvas) { _canvas->deleteSprite(); delete _canvas; _canvas = nullptr; }
    _haveCanvas = false;
}

} // namespace MOONCAKE::APPS
