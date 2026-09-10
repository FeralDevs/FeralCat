/**
 * @file  app_10.cpp
 * @brief MeowGotchi app — see app_10.h.
 *
 * Draws every frame into an off-screen sprite and blits once (no flicker).
 * Opens to a menu (Start/Pause/Mode); hunting does not auto-start.
 * Exit is the firmware-wide gesture: hold B (the launcher catches it and
 * repaints the menu). The app never calls close() itself — doing so tears the
 * app down without the launcher's repaint and leaves a blank screen.
 */
#include "app_10.h"
#include <Arduino.h>
#include "../app_common/mk_tui.h"

namespace MOONCAKE::APPS
{

App10::App10(DEVICES* device) : _device(device)
{
    setAppInfo().name = "MeowGotchi";
}

void App10::onOpen()
{
    _device->wifi.begin();
    _device->sd.begin();                 /* pcap capture needs the SD card */

    _canvas.setColorDepth(16);
    _canvas.setPsram(true);
    _haveCanvas = _canvas.createSprite(MK_LAYOUT::W, MK_LAYOUT::H);

    _hunter.begin(_device->sd.isReady());
    _hunter.pause();                     /* idle until the user presses Start */

    _page       = Page::Face;            /* open on the main face screen */
    _menuSel    = 0;
    _prevShakes = 0;
    _prevAct    = 0;
    _lastAct    = millis();
    _dirty      = true;
}

MeowGotchi::Mood App10::_mood()
{
    const HunterStats& s = _hunter.stats();
    const uint32_t now = millis();

    if (s.shakes > _prevShakes) { _prevShakes = s.shakes; _lastShake = now; }
    uint16_t act = s.aps + s.stas;
    if (act > _prevAct) { _prevAct = act; _lastAct = now; }

    if (_lastShake && now - _lastShake < 5000)        return MeowGotchi::Mood::Excited;
    if (_hunter.aggressive())                         return MeowGotchi::Mood::Cool;
    if (!_hunter.sdReady() && s.eapol > 0)            return MeowGotchi::Mood::Sad;
    if (_lastAct && now - _lastAct < 4000 && act > 0) return MeowGotchi::Mood::Hunt;
    if (act > 0)                                      return MeowGotchi::Mood::Bored;
    return MeowGotchi::Mood::Sleep;
}

template<typename LCD>
void App10::_renderFace(LCD& lcd)
{
    const HunterStats& s = _hunter.stats();
    MeowGotchi::View v;
    v.mood       = _mood();
    v.channel    = s.channel;
    v.aps        = s.aps;
    v.stas       = s.stas;
    v.shakes     = s.shakes;
    v.deauths    = s.deauths;
    v.uptime_s   = _hunter.uptime_s();
    v.aggressive = _hunter.aggressive();
    v.blink      = _blink;
    v.footA      = _hunter.running() ? "Pause" : "Start";
    v.footB      = "Menu";
    if (!_hunter.running()) v.line = "paused - [A] to start";
    MeowGotchi::drawFace(lcd, v);
}

template<typename LCD>
void App10::_renderMenu(LCD& lcd)
{
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "MeowGotchi");
    MK_TUI::drawMenuItem(lcd, 0, "Hunt",
                         _hunter.running() ? "PAUSE" : "START", _menuSel == 0);
    MK_TUI::drawMenuItem(lcd, 1, "Mode",
                         _hunter.aggressive() ? "AGGRESSIVE" : "passive", _menuSel == 1);
    char hs[24];
    snprintf(hs, sizeof(hs), "%u", _hunter.stats().shakes);
    MK_TUI::drawMenuItem(lcd, 2, "Handshakes", hs, _menuSel == 2);
    MK_TUI::drawFooter(lcd, "Select", "Back");
}

void App10::_present(bool face)
{
    if (_haveCanvas) {
        if (face) _renderFace(_canvas); else _renderMenu(_canvas);
        _canvas.pushSprite(&_device->Lcd, 0, 0);
    } else {
        if (face) _renderFace(_device->Lcd); else _renderMenu(_device->Lcd);
    }
}

void App10::onRunning()
{
    _device->button.update();
    _device->button.tick();

    _hunter.loop();

    const uint32_t now = millis();

    if (_page == Page::Face) {
        /* A = quick Start/Pause; short B = menu; hold B = exit (launcher). */
        if (_device->button.A.pressed()) {
            if (_hunter.running()) _hunter.pause(); else _hunter.resume();
            _dirty = true;
        }
        if (_device->button.B.pressed()) { _page = Page::Menu; _dirty = true; }
        /* Brief eye-blink every ~3 s. */
        if (!_blink && now - _blinkAt > 3000)        { _blink = true;  _blinkAt = now; _dirty = true; }
        else if (_blink && now - _blinkAt > 150)     { _blink = false; _dirty = true; }

        if (_dirty || now - _lastDraw >= 1000) { _present(true); _lastDraw = now; _dirty = false; }
    }
    else { /* Page::Menu */
        if (_device->button.Up.pressed())
            { _menuSel = (_menuSel + MENU_ROWS - 1) % MENU_ROWS; _dirty = true; }
        if (_device->button.Down.pressed())
            { _menuSel = (_menuSel + 1) % MENU_ROWS; _dirty = true; }
        if (_device->button.A.pressed()) {
            if (_menuSel == 0) {
                if (_hunter.running()) _hunter.pause();
                else { _hunter.resume(); _page = Page::Face; }
            } else if (_menuSel == 1) {
                _hunter.setAggressive(!_hunter.aggressive());
            } else {
                _page = Page::Face;
            }
            _dirty = true;
        }
        if (_device->button.B.pressed()) { _page = Page::Face; _dirty = true; }

        if (_dirty) { _present(false); _dirty = false; }
    }

    delay(20);
}

void App10::onClose()
{
    _hunter.stop();
    if (_haveCanvas) { _canvas.deleteSprite(); _haveCanvas = false; }
    /* No fillScreen: the launcher repaints the menu on exit (returnToUI). */
}

} // namespace MOONCAKE::APPS
