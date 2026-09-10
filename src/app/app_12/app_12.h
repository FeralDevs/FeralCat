/**
 * @file  app_12.h
 * @brief App12 — Flash Mode: reboot into USB ROM download mode from software,
 *        so a new firmware can be flashed without holding the BOOT button.
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
        bool _drawn = false;
        void _drawConfirm();
        void _enterDownloadMode();
    };
}
