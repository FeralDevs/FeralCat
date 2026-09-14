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
#include "../../system/screenshot.h"

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

    _haveCanvas = false;
    _canvas = new lgfx::LGFX_Sprite(&_device->Lcd);
    if (_canvas) {
        _canvas->setColorDepth(16);
        _canvas->setPsram(true);
        _haveCanvas = _canvas->createSprite(MK_LAYOUT::W, MK_LAYOUT::H);
    }

    _hunter.begin(_device->sd.isReady());
    _hunter.pause();                     /* idle until the user presses Start */

    _page       = Page::Face;            /* open on the main face screen */
    _menuSel    = 0;
    _prevShakes = 0;
    _prevAct    = 0;
    _xpShakes   = 0;                      /* hunter stats reset by begin() above */
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

template<typename LCD>
void App10::_renderInfo(LCD& lcd)
{
    const HunterStats& s = _hunter.stats();
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "Handshakes");

    const int LX = MK_LAYOUT::PAD, VX = 110;
    auto field = [&](int y, const char* lbl, const char* val, uint32_t vc) {
        lcd.setFont(&fonts::efontCN_16);
        lcd.setTextColor((uint32_t)MK_PAL::TEXT_SEC, (uint32_t)MK_PAL::BLACK);
        lcd.setCursor(LX, y); lcd.printf("%s", lbl);
        lcd.setTextColor(vc, (uint32_t)MK_PAL::BLACK);
        lcd.setCursor(VX, y); lcd.printf("%s", val);
    };

    char buf[32];
    int y = MK_LAYOUT::CONTENT_Y + 12;
    snprintf(buf, sizeof(buf), "%u", s.shakes);
    field(y, "Captured", buf, s.shakes ? MK_PAL::OK : MK_PAL::TEXT_PRI);  y += 28;
    snprintf(buf, sizeof(buf), "%u", s.eapol);
    field(y, "EAPOL seen", buf, MK_PAL::TEXT_PRI);                        y += 28;
    field(y, "SD card", _hunter.sdReady() ? "ready" : "no card",
          _hunter.sdReady() ? MK_PAL::OK : MK_PAL::ERR);                  y += 28;
    field(y, "Saved to", "/handshakes/", MK_PAL::TEXT_PRI);

    MK_TUI::drawFooter(lcd, "", "Back");
}

void App10::_present()
{
    LGFX_Class& dst = _device->Lcd;
    if (_haveCanvas) {
        if      (_page == Page::Face) _renderFace(*_canvas);
        else if (_page == Page::Menu) _renderMenu(*_canvas);
        else                          _renderInfo(*_canvas);
        _canvas->pushSprite(&dst, 0, 0);
    } else {
        if      (_page == Page::Face) _renderFace(dst);
        else if (_page == Page::Menu) _renderMenu(dst);
        else                          _renderInfo(dst);
    }
}

void App10::onRunning()
{
    _device->button.update();
    _device->button.tick();

    _hunter.loop();

    /* Award device-wide XP for any new handshakes (the +15 "find" bonus). The
     * hunter's shake count only climbs, so the delta is safe even when frames
     * or pages are skipped. */
    {
        uint16_t sh = _hunter.stats().shakes;
        if (sh > _xpShakes) { meow_xp_add_handshake(sh - _xpShakes); _xpShakes = sh; }
    }

    const uint32_t now = millis();

    /* ── Input first (may change _page) ── */
    if (_page == Page::Face) {
        /* A = quick Start/Pause; short B = menu; hold B = exit (launcher). */
        if (_device->button.A.pressed()) {
            if (_hunter.running()) _hunter.pause(); else _hunter.resume();
            _dirty = true;
        }
        if (_device->button.B.pressed()) { _page = Page::Menu; _dirty = true; }
        /* Brief eye-blink every ~3 s. */
        if (!_blink && now - _blinkAt > 3000)    { _blink = true;  _blinkAt = now; _dirty = true; }
        else if (_blink && now - _blinkAt > 150) { _blink = false; _dirty = true; }
    }
    else if (_page == Page::Menu) {
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
                _page = Page::Info;          /* Handshakes → info page */
            }
            _dirty = true;
        }
        if (_device->button.B.pressed()) { _page = Page::Face; _dirty = true; }
    }
    else { /* Page::Info */
        if (_device->button.B.pressed()) { _page = Page::Menu; _dirty = true; }
    }

    /* ── Draw the CURRENT page (so a page switch this frame renders the new
     *    page, not the old one — otherwise the switch's dirty flag is spent
     *    drawing the page we just left). ── */
    if (_page == Page::Face) {
        if (_dirty || now - _lastDraw >= 1000) { _present(); _lastDraw = now; _dirty = false; }
    } else {
        if (_dirty) { _present(); _dirty = false; }
    }

    if (_haveCanvas) screenshot_tui_tick(_device, _canvas);   /* Up+Down = save to SD */
    delay(20);
}

void App10::onClose()
{
    _hunter.stop();
    if (_canvas) { _canvas->deleteSprite(); delete _canvas; _canvas = nullptr; }
    _haveCanvas = false;
    /* No fillScreen: the launcher repaints the menu on exit (returnToUI). */
}

} // namespace MOONCAKE::APPS
