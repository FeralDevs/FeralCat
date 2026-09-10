/**
 * @file  app_10.h
 * @brief App10 — MeowGotchi: a pwnagotchi-style WiFi hunter with a cat face.
 *        Passive sniffing + WPA-handshake capture by default; opt-in deauth.
 */
#pragma once
#include <mooncake.h>
#include "../../bsp/devices.h"
#include "wifi_hunter.h"
#include "meowgotchi_ui.h"

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

        enum class Page : uint8_t { Face, Menu } _page = Page::Face;
        int      _menuSel   = 0;

        uint32_t _lastDraw  = 0;
        uint32_t _lastShake = 0;   /* millis of last new handshake  */
        uint32_t _lastAct   = 0;   /* millis of last new AP/client  */
        uint32_t _blinkAt   = 0;
        uint16_t _prevShakes= 0;
        uint16_t _prevAct   = 0;
        bool     _blink     = false;
        bool     _dirty     = true;

        MeowGotchi::Mood _mood();
        void _drawFace();
        void _drawMenu();
    };
}
