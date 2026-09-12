/**
 * @file  app_16.h
 * @brief App16 — Probe Sniffer: passively log 802.11 probe-request frames from
 *        nearby devices (source MAC + requested SSID + signal).
 */
#pragma once
#include <mooncake.h>
#include <LovyanGFX.hpp>
#include "../../bsp/devices.h"
#include "probe_monitor.h"
#include "probe_ui.h"

using namespace mooncake;

namespace MOONCAKE::APPS
{
    class App16 : public AppAbility {
    public:
        App16(DEVICES* device);
        void onOpen() override;
        void onRunning() override;
        void onClose() override;

    private:
        DEVICES*     _device = nullptr;
        ProbeMonitor _mon;

        lgfx::LGFX_Sprite* _canvas = nullptr;   /* lazily allocated in onOpen */
        bool _haveCanvas = false;

        uint32_t _lastDraw = 0;
        bool     _dirty    = true;
        bool     _graphView = false;   /* false = device list, true = graph */
        uint16_t   _histbuf[ProbeMonitor::HIST];
        ProbeEntry _devbuf[ProbeMonitor::MAXDEV];

        template<typename LCD> void _render(LCD& lcd);
        void _present();
    };
}
