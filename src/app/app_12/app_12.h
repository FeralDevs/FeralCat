/**
 * @file  app_12.h
 * @brief App12 — Firmware: update from an SD‑card image (dual‑OTA), or reboot
 *        into USB download mode. No BOOT button, no computer for SD updates.
 */
#pragma once
#include <mooncake.h>
#include "../../bsp/devices.h"

using namespace mooncake;

namespace MOONCAKE::APPS
{
    class App12 : public AppAbility {
    public:
        App12(DEVICES* device);
        void onOpen() override;
        void onRunning() override;
        void onClose() override;

    private:
        DEVICES* _device = nullptr;
        int  _sel   = 0;
        bool _dirty = true;
        static constexpr int ROWS = 2;

        void _drawMenu();
        void _sdUpdate();
        void _usbDownload();
        void _modal(const char* line1, const char* line2, uint32_t color);
    };
}
