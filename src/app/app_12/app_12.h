/**
 * @file  app_12.h
 * @brief App12 — Firmware: update over WiFi (pull the latest release from
 *        GitHub onto the SD card), update from an SD‑card image (dual‑OTA), or
 *        reboot into USB download mode. No BOOT button, no computer needed.
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
        static constexpr int ROWS = 3;

        void _drawMenu();
        void _wifiUpdate();
        void _sdUpdate();
        void _usbDownload();
        void _modal(const char* line1, const char* line2, uint32_t color);

        /* WiFi‑OTA helpers (see app_12_wifi.cpp). */
        bool _wifiConnect();                       /* join saved network; status screen */
        bool _fetchLatestTag(char* out, size_t n); /* read latest release tag from GitHub */
        bool _downloadToSD(const char* tag);       /* stream firmware.bin → /firmware.bin */
        void _statusScreen(const char* line, uint32_t color);
        bool _confirm(const char* line1, const char* line2,
                      const char* btnA, const char* btnB);
    };
}
