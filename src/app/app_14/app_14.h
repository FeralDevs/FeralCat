/**
 * @file  app_14.h
 * @brief App14 — BLE Spam Detector: passively watch for BLE-spam ad floods.
 */
#pragma once
#include <mooncake.h>
#include <LovyanGFX.hpp>
#include "../../bsp/devices.h"
#include "ble_spam_monitor.h"
#include "ble_spam_ui.h"

using namespace mooncake;

namespace MOONCAKE::APPS
{
    class App14 : public AppAbility {
    public:
        App14(DEVICES* device);
        void onOpen() override;
        void onRunning() override;
        void onClose() override;

    private:
        DEVICES*       _device = nullptr;
        BleSpamMonitor _mon;

        lgfx::LGFX_Sprite* _canvas = nullptr;
        bool _haveCanvas = false;

        uint32_t _lastDraw = 0;
        bool     _dirty    = true;
        uint16_t _histbuf[BleSpamMonitor::HIST];

        template<typename LCD> void _render(LCD& lcd);
        void _present();
    };
}
