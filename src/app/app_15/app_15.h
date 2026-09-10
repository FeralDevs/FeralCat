/**
 * @file  app_15.h
 * @brief App15 — Rogue Radar: Evil-Twin scan + beacon/probe flood (Karma) monitor.
 */
#pragma once
#include <mooncake.h>
#include <LovyanGFX.hpp>
#include "../../bsp/devices.h"
#include "beacon_flood.h"
#include "rogue_ui.h"

using namespace mooncake;

namespace MOONCAKE::APPS
{
    class App15 : public AppAbility {
    public:
        App15(DEVICES* device);
        void onOpen() override;
        void onRunning() override;
        void onClose() override;

    private:
        DEVICES*    _device = nullptr;
        BeaconFlood _flood;

        lgfx::LGFX_Sprite* _canvas = nullptr;
        bool _haveCanvas = false;

        enum class Mode : uint8_t { Twins, Flood } _mode = Mode::Twins;

        static constexpr int MAXTW = 40;
        RogueUI::TwinRow _tw[MAXTW];
        int  _tw_n    = 0;
        int  _sel     = 0;
        int  _scroll  = 0;
        int  _flagged = 0;
        bool _scanning = false;
        bool _dirty   = true;
        uint32_t _lastDraw = 0;
        uint16_t _histbuf[BeaconFlood::HIST];

        void _scan();
        template<typename LCD> void _render(LCD& lcd);
        void _present();
    };
}
