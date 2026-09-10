/**
 * @file  app_10.cpp
 * @brief MeowGotchi app — see app_10.h.
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
    /* Ensure the radio is up, then start hunting passively. */
    _device->wifi.begin();
    _device->sd.begin();                 /* pcap capture needs the SD card */
    _hunter.begin(_device->sd.isReady());

    _page = Page::Face;
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

    if (_lastShake && now - _lastShake < 5000)       return MeowGotchi::Mood::Excited;
    if (_hunter.aggressive())                        return MeowGotchi::Mood::Cool;
    if (!_hunter.sdReady() && s.eapol > 0)           return MeowGotchi::Mood::Sad;
    if (_lastAct && now - _lastAct < 4000 && act > 0) return MeowGotchi::Mood::Hunt;
    if (act > 0)                                     return MeowGotchi::Mood::Bored;
    return MeowGotchi::Mood::Sleep;
}

void App10::_drawFace()
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
    MeowGotchi::drawFace(_device->Lcd, v);
}

void App10::_drawMenu()
{
    LGFX_Class& lcd = _device->Lcd;
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "MeowGotchi");
    MK_TUI::drawMenuItem(lcd, 0, "Mode",
                         _hunter.aggressive() ? "AGGRESSIVE" : "passive", _menuSel == 0);
    char hs[24];
    snprintf(hs, sizeof(hs), "%u saved", _hunter.stats().shakes);
    MK_TUI::drawMenuItem(lcd, 1, "Handshakes", hs, _menuSel == 1);
    MK_TUI::drawMenuItem(lcd, 2, "Exit", _hunter.sdReady() ? "" : "no SD", _menuSel == 2);
    MK_TUI::drawFooter(lcd, "Select", "Back");
}

void App10::onRunning()
{
    _device->button.update();
    _device->button.tick();

    _hunter.loop();

    const uint32_t now = millis();

    if (_page == Page::Face) {
        if (_device->button.A.pressed()) { _page = Page::Menu; _menuSel = 0; _dirty = true; }
        else if (_device->button.B.pressed()) { close(); return; }

        /* Blink briefly every ~3 s. */
        if (now - _blinkAt > 3000)      { _blink = true;  _blinkAt = now; _dirty = true; }
        else if (_blink && now - _blinkAt > 150) { _blink = false; _dirty = true; }

        if (_dirty || now - _lastDraw > 500) { _drawFace(); _lastDraw = now; _dirty = false; }
    }
    else { /* Page::Menu */
        if (_device->button.Up.pressed())   { _menuSel = (_menuSel + 2) % 3; _dirty = true; }
        if (_device->button.Down.pressed()) { _menuSel = (_menuSel + 1) % 3; _dirty = true; }
        if (_device->button.A.pressed()) {
            if (_menuSel == 0) { _hunter.setAggressive(!_hunter.aggressive()); _dirty = true; }
            else if (_menuSel == 2) { close(); return; }
        }
        if (_device->button.B.pressed()) { _page = Page::Face; _dirty = true; }

        if (_dirty) { _drawMenu(); _dirty = false; }
    }

    delay(20);
}

void App10::onClose()
{
    _hunter.stop();
    _device->Lcd.fillScreen(TFT_BLACK);
}

} // namespace MOONCAKE::APPS
