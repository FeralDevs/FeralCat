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
    if (_radar) {
        TrackerUI::RadarView radar;
        radar.target = _finder.target();
        radar.hasTarget = _finder.hasTarget();
        radar.running = _mon.running();
        radar.starting = _mon.starting();
        radar.error = _mon.error();
        radar.now_ms = millis();
        radar.age_ms = _finder.age(radar.now_ms);
        radar.strength = _finder.strength();
        radar.trend_ready = _finder.trend(radar.now_ms, radar.running, radar.trend);
        switch (_finder.state(radar.now_ms, radar.running)) {
            case TrackerFinder::State::Live: radar.state = TrackerUI::SignalState::Live; break;
            case TrackerFinder::State::Waiting: radar.state = TrackerUI::SignalState::Waiting; break;
            case TrackerFinder::State::Lost: radar.state = TrackerUI::SignalState::Lost; break;
            case TrackerFinder::State::Paused: radar.state = TrackerUI::SignalState::Paused; break;
        }
        radar.history_count = static_cast<uint8_t>(_finder.historyCount());
        for (unsigned i = 0; i < radar.history_count; ++i)
            radar.history[i] = _finder.history(i);
        TrackerUI::drawRadar(lcd, radar);
        return;
    }
    TrackerUI::View v;
    v.nearby     = s.nearby;
    v.persistent = s.persistent;
    v.alert      = s.alert;
    v.running    = _mon.running();
    v.starting   = _mon.starting();
    v.error      = _mon.error();
    v.now_ms     = millis();
    v.selected   = _selection.index();
    v.first      = (v.selected / 4) * 4;
    TrackerUI::drawList(lcd, v, _trkbuf, _trackerCount);
}

void App17::_refresh()
{
    _trackerCount = _mon.trackers(_trkbuf, TrackerMonitor::MAXTRK);
    if (!_radar) {
        _selection.reconcile(_trkbuf, _trackerCount);
        _mon.select(_selection.id()); // keep the focused candidate through busy scans
    }
    if (_radar) {
        TrackerEntry current{};
        const bool found = _mon.tracker(_finder.target().id, current);
        // Read the clock after the snapshot: a BLE callback may have just run.
        _finder.update(found ? &current : nullptr, millis(), _mon.running());
    }
}

void App17::_toggleScan()
{
    if (_mon.error()) {
        // A new BLE session allocates new identities; never reuse an old lock.
        _radar = false;
        _selection.reset();
        _finder.reset();
        _trackerCount = 0;
        _mon.begin();
    } else if (_mon.running() || _mon.starting()) _mon.pause();
    else _mon.resume();
    _dirty = true;
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
    _selection.reset();
    _finder.reset();
    _radar = false;
    _trackerCount = 0;
    _inputReady = false; // Consume the A press which launched this app.
    _lastDraw = 0;
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

    const uint32_t now = millis();
    // Read each edge once: the button API consumes the transition flag.
    const bool a = _device->button.A.pressed();
    const bool b = _device->button.B.pressed();
    const bool up = _device->button.Up.pressed();
    const bool down = _device->button.Down.pressed();
    const bool left = _device->button.Left.pressed();
    const bool right = _device->button.Right.pressed();
    if (!_inputReady) {
        _inputReady = _device->button.A.state() == Button_Class::RELEASED &&
                      _device->button.B.state() == Button_Class::RELEASED;
    } else {
        // Act on the rows actually displayed, not a freshly re-sorted list.
        const bool screenshotChord = _device->button.Up.state() == Button_Class::PRESSED &&
                                     _device->button.Down.state() == Button_Class::PRESSED;
        if (!_radar && !screenshotChord && (up != down)) {
            _selection.move(_trkbuf, _trackerCount, up ? -1 : 1);
            _dirty = true;
        }
        if (b && _radar) {
            _radar = false;
            _mon.select(0);
            _dirty = true;
        } else if (a) {
            if (_radar || _mon.error()) _toggleScan();
            else if (_trackerCount > 0) {
                TrackerEntry selected{};
                if (_mon.select(_selection.id()) && _mon.tracker(_selection.id(), selected)) {
                    _finder.select(selected, millis(), _mon.running());
                    _radar = true;
                    _dirty = true;
                }
            }
        }
        if (!a && !b && (left || right)) _toggleScan();
    }

    if (_dirty || uint32_t(now - _lastDraw) >= 200) {
        _refresh();
        _present();
        _lastDraw = now;
        _dirty = false;
    }

    if (_haveCanvas) screenshot_tui_tick(_device, _canvas);   /* Up+Down = save to SD */
    delay(20);
}

void App17::onClose()
{
    _mon.stop();
    _finder.reset();
    if (_canvas) { _canvas->deleteSprite(); delete _canvas; _canvas = nullptr; }
    _haveCanvas = false;
}

} // namespace MOONCAKE::APPS
