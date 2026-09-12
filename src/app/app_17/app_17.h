/**
 * @file  app_17.h
 * @brief App17 — Tracker Detector: scan BLE for AirTag/Tile/SmartTag item
 *        trackers and flag any that keep following you.
 */
#pragma once
#include <mooncake.h>
#include <LovyanGFX.hpp>
#include "../../bsp/devices.h"
#include "tracker_monitor.h"
#include "tracker_ui.h"

using namespace mooncake;

namespace MOONCAKE::APPS
{
    class App17 : public AppAbility {
    public:
        App17(DEVICES* device);
        void onOpen() override;
        void onRunning() override;
        void onClose() override;

    private:
        DEVICES*       _device = nullptr;
        TrackerMonitor _mon;

        lgfx::LGFX_Sprite* _canvas = nullptr;   /* lazily allocated in onOpen */
        bool _haveCanvas = false;

        uint32_t _lastDraw = 0;
        bool     _dirty    = true;
        TrackerEntry _trkbuf[TrackerMonitor::MAXTRK];

        template<typename LCD> void _render(LCD& lcd);
        void _present();
    };
}
