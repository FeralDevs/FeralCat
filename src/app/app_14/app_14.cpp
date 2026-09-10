/**
 * @file  app_14.cpp
 * @brief BLE Spam Detector app — see app_14.h.
 */
#include "app_14.h"
#include <Arduino.h>
#include "../app_common/mk_tui.h"

namespace MOONCAKE::APPS
{

App14::App14(DEVICES* device) : _device(device)
{
    setAppInfo().name = "BleSpamDetect";
}

template<typename LCD>
void App14::_render(LCD& lcd)
{
    const BleSpamStats& s = _mon.stats();
    _mon.history(_histbuf);

    BleSpamUI::View v;
    v.total   = s.total;
    v.rate    = s.rate;
    v.peak    = s.peak;
    v.alert   = s.alert;
    v.running = _mon.running();
    v.apple   = s.apple;
    v.google  = s.google;
    v.ms      = s.ms;
    v.samsung = s.samsung;
    v.hist    = _histbuf;
    v.histLen = BleSpamMonitor::HIST;
    v.threshold = BleSpamMonitor::ALERT_THRESHOLD;
    BleSpamUI::draw(lcd, v);
}

void App14::_present()
{
    if (_haveCanvas) {
        _render(*_canvas);
        _canvas->pushSprite(&_device->Lcd, 0, 0);
    } else {
        _render(_device->Lcd);
    }
}

void App14::onOpen()
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

void App14::onRunning()
{
    _device->button.update();
    _device->button.tick();

    _mon.loop();

    if (_device->button.A.pressed()) {
        if (_mon.running()) _mon.pause(); else _mon.resume();
        _dirty = true;
    }

    const uint32_t now = millis();
    if (_dirty || now - _lastDraw >= 500) { _present(); _lastDraw = now; _dirty = false; }

    delay(20);
}

void App14::onClose()
{
    _mon.stop();
    if (_canvas) { _canvas->deleteSprite(); delete _canvas; _canvas = nullptr; }
    _haveCanvas = false;
}

} // namespace MOONCAKE::APPS
