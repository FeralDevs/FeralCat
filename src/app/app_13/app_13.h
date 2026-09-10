/**
 * @file  app_13.h
 * @brief App13 — Deauth Detector: passively watch for deauth/disassoc bursts.
 */
#pragma once
#include <mooncake.h>
#include <LovyanGFX.hpp>
#include "../../bsp/devices.h"
#include "deauth_monitor.h"
#include "deauth_ui.h"

using namespace mooncake;

namespace MOONCAKE::APPS
{
    class App13 : public AppAbility {
    public:
        App13(DEVICES* device);
        void onOpen() override;
        void onRunning() override;
        void onClose() override;

    private:
        DEVICES*      _device = nullptr;
        DeauthMonitor _mon;

        lgfx::LGFX_Sprite* _canvas = nullptr;   /* lazily allocated in onOpen */
        bool _haveCanvas = false;

        uint32_t _lastDraw = 0;
        bool     _dirty    = true;
        uint16_t _histbuf[DeauthMonitor::HIST];

        template<typename LCD> void _render(LCD& lcd);
        void _present();
    };
}
