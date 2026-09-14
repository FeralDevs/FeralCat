/**
 * @file  app_10.h
 * @brief App10 — MeowGotchi: a pwnagotchi-style WiFi hunter with a cat face.
 *        Passive sniffing + WPA-handshake capture by default; opt-in deauth.
 */
#pragma once
#include <mooncake.h>
#include <LovyanGFX.hpp>
#include "../../bsp/devices.h"
#include "wifi_hunter.h"
#include "meowgotchi_ui.h"
#include "../../system/meow_xp.h"

using namespace mooncake;

namespace MOONCAKE::APPS
{
    class App10 : public AppAbility {
    public:
        App10(DEVICES* device);
        void onOpen() override;
        void onRunning() override;
        void onClose() override;

    private:
        DEVICES*   _device = nullptr;
        WifiHunter _hunter;

        /* Off-screen buffer — the whole frame is drawn here then blitted once,
         * so the screen never flickers from a mid-frame clear. Allocated in
         * onOpen (NOT a value member): constructing an LGFX_Sprite at boot, when
         * the app registry is built, hangs startup. */
        lgfx::LGFX_Sprite* _canvas = nullptr;
        bool     _haveCanvas = false;

        enum class Page : uint8_t { Face, Menu, Info } _page = Page::Face;
        int      _menuSel   = 0;
        static constexpr int MENU_ROWS = 3;   /* Start/Pause, Mode, Handshakes */

        uint32_t _lastDraw  = 0;
        uint32_t _lastShake = 0;   /* millis of last new handshake  */
        uint32_t _lastAct   = 0;   /* millis of last new AP/client  */
        uint32_t _blinkAt   = 0;
        uint16_t _prevShakes= 0;
        uint16_t _prevAct   = 0;
        bool     _blink     = false;
        bool     _dirty     = true;

        /* Handshakes already banked into system XP (award the delta each frame). */
        uint16_t _xpShakes  = 0;

        MeowGotchi::Mood _mood();
        template<typename LCD> void _renderFace(LCD& lcd);
        template<typename LCD> void _renderMenu(LCD& lcd);
        template<typename LCD> void _renderInfo(LCD& lcd);
        void _present();   /* renders whichever page is active */
    };
}
