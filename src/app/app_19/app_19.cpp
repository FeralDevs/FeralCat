/**
 * @file  app_19.cpp
 * @brief MeowPlayer (native, AudioService-backed) — see app_19.h.
 *
 * The meow::media::AudioService owns the ES8311 codec, the ESP32-audioI2S
 * decoder and I2S on a core-0 worker; it scans /music into a catalog. This app is
 * a thin UI: it sends Commands (Play/Pause/Stop/Volume/Seek) and renders the
 * Status snapshot. PA_EN (speaker vs 3.5mm jack) is the only bit we drive here.
 */
#include "app_19.h"
#include <Arduino.h>
#include <cstring>
#include "../app_common/mk_tui.h"
#include "meowplayer_ui.h"
#include "../../bsp/config.h"

using meow::media::Action;
using meow::media::Command;
using meow::media::Page;

namespace MOONCAKE::APPS
{

App19::App19(DEVICES* device) : _device(device)
{
    setAppInfo().name = "MeowPlayer";
}

void App19::_setAmp(bool on)
{
    _device->io_exp.digitalWrite(HAL_IOEXP_PA_EN, on ? HIGH : LOW);
    _ampOn = on;
}

/* Pull the whole track list (up to MAXT) into a local cache once a scan
 * completes, so the Songs menu is simple index access. */
void App19::_refreshCatalog()
{
    _ntracks = 0;
    size_t off = 0;
    while (_ntracks < MAXT) {
        Page pg{};
        if (!_svc.tracks(0, off, meow::media::MaxPage, pg) || pg.count == 0) break;
        for (size_t k = 0; k < pg.count && _ntracks < MAXT; k++) {
            _ids[_ntracks] = pg.entries[k].id;
            strncpy(_titles[_ntracks], pg.entries[k].title, sizeof(_titles[0]) - 1);
            _titles[_ntracks][sizeof(_titles[0]) - 1] = '\0';
            _titlePtrs[_ntracks] = _titles[_ntracks];
            _ntracks++;
        }
        off += pg.count;
        if (off >= pg.total) break;
    }
    _cachedGen = _st.generation;
}

void App19::_play(int idx)
{
    if (idx < 0 || idx >= _ntracks) return;
    if (_ampOn) _setAmp(true);
    _svc.command(Command{Action::Play, (int32_t)_ids[idx]});
    _cur = idx;
}

void App19::_buildMenu()
{
    snprintf(_songsItem, sizeof(_songsItem), "Songs (%d)", _ntracks);
    snprintf(_outItem,   sizeof(_outItem),   "Output: %s", _ampOn ? "Speaker" : "Jack");
    _menuItems[0] = _songsItem;
    _menuItems[1] = _outItem;
}

void App19::onOpen()
{
    _haveCanvas = false;
    _canvas = new lgfx::LGFX_Sprite(&_device->Lcd);
    if (_canvas) {
        _canvas->setColorDepth(16);
        _canvas->setPsram(true);
        _haveCanvas = _canvas->createSprite(MK_LAYOUT::W, MK_LAYOUT::H);
    }

    _svcReady   = _svc.begin(_device);   /* spawns worker; scans /music async */
    _ampOn      = true;                   /* begin() drives PA_EN HIGH       */
    _screen     = Screen::Now;
    _ntracks    = 0;
    _cur        = -1;
    _cachedGen  = 0xFFFFFFFF;
    _lastFinished = 0;
    _lastPoll = _lastDraw = 0;
    Serial.printf("[MeowPlayer] service begin: %s\n", _svcReady ? "OK" : "FAIL");
}

void App19::_present()
{
    if (_screen == Screen::Menu || _screen == Screen::Songs) {
        MeowPlayer::MenuView m;
        if (_screen == Screen::Menu) {
            m.title = "Menu"; m.items = _menuItems; m.count = 2; m.sel = _msel; m.top = 0;
        } else {
            m.title = "Songs"; m.items = _titlePtrs; m.count = _ntracks; m.sel = _ssel; m.top = _stop;
        }
        if (_haveCanvas) { MeowPlayer::drawMenu(*_canvas, m); _canvas->pushSprite(&_device->Lcd, 0, 0); }
        else             MeowPlayer::drawMenu(_device->Lcd, m);
        return;
    }

    MeowPlayer::View v;
    v.spkReady = _svcReady;
    v.ampOn    = _ampOn;
    v.playing  = !strcmp(_st.state, "playing");
    v.title    = _st.title[0] ? _st.title
               : (!strcmp(_st.state, "scanning") ? "Scanning /music..." : "(no track)");
    v.posSec   = (int)_st.position;
    v.durSec   = (int)_st.duration;
    v.vol      = _st.volume;
    v.volMax   = 100;
    v.diag     = _diag;
    if (_haveCanvas) { MeowPlayer::drawNowPlaying(*_canvas, v); _canvas->pushSprite(&_device->Lcd, 0, 0); }
    else             MeowPlayer::drawNowPlaying(_device->Lcd, v);
}

void App19::onRunning()
{
    _device->button.update();
    _device->button.tick();

    /* Poll the service snapshot (~4 Hz). */
    uint32_t now = millis();
    if (_svcReady && now - _lastPoll >= 250) {
        _lastPoll = now;
        _svc.status(_st);
        /* Cache the track list once a scan generation is ready. */
        if (_st.generation != _cachedGen && strcmp(_st.state, "scanning")) _refreshCatalog();
        /* Auto-advance on natural end. */
        if (_st.finished != _lastFinished) {
            _lastFinished = _st.finished;
            if (_cur + 1 < _ntracks) _play(_cur + 1);
        }
        snprintf(_diag, sizeof(_diag), "%s %lu/%lus vol%u trk%d/%d",
                 _st.state, (unsigned long)_st.position, (unsigned long)_st.duration,
                 _st.volume, _cur + 1, _ntracks);
    }

    const bool a  = _device->button.A.pressed();
    const bool b  = _device->button.B.pressed();
    const bool up = _device->button.Up.pressed();
    const bool dn = _device->button.Down.pressed();
    const bool lf = _device->button.Left.pressed();
    const bool rt = _device->button.Right.pressed();
    const bool acted = a || b || up || dn || lf || rt;

    auto vol = [&](int d) {
        int nv = (int)_st.volume + d; if (nv < 0) nv = 0; if (nv > 100) nv = 100;
        _svc.command(Command{Action::Volume, nv}); _st.volume = (uint8_t)nv;
    };

    if (_screen == Screen::Now) {
        if (a)  _svc.command(Command{Action::Pause, 0});
        if (b)  { _buildMenu(); _msel = 0; _screen = Screen::Menu; }
        if (up) vol(+5);
        if (dn) vol(-5);
        if (lf) _svc.command(Command{Action::Seek, (int)_st.position > 5 ? (int)_st.position - 5 : 0});
        if (rt) _svc.command(Command{Action::Seek, (int)_st.position + 5});
    } else if (_screen == Screen::Menu) {
        if (up && _msel > 0) _msel--;
        if (dn && _msel < 1) _msel++;
        if (a) {
            if (_msel == 0) { _ssel = (_cur >= 0) ? _cur : 0; _stop = 0; _screen = Screen::Songs; }
            else            { _setAmp(!_ampOn); _buildMenu(); }
        }
        if (b) _screen = Screen::Now;
    } else { /* Songs */
        if (up && _ssel > 0) _ssel--;
        if (dn && _ssel < _ntracks - 1) _ssel++;
        if (_ssel < _stop) _stop = _ssel;
        if (_ssel > _stop + 7) _stop = _ssel - 7;
        if (a && _ntracks > 0) { _play(_ssel); _screen = Screen::Now; }
        if (b) _screen = Screen::Now;
    }

    if (acted || now - _lastDraw >= 300) { _present(); _lastDraw = now; }
}

void App19::onClose()
{
    _svc.end();
    _setAmp(false);
    if (_canvas) { _canvas->deleteSprite(); delete _canvas; _canvas = nullptr; }
    _haveCanvas = false;
}

} // namespace MOONCAKE::APPS
