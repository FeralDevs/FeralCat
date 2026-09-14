/**
 * @file  app_20.h
 * @brief App20 — ELF Test (spike). Loads /apps/hello/app.elf from SD and runs it,
 *        showing the return code. Proof-of-concept for native SD apps.
 */
#pragma once
#include <mooncake.h>
#include <LovyanGFX.hpp>
#include "../../bsp/devices.h"

using namespace mooncake;

namespace MOONCAKE::APPS
{
    class App20 : public AppAbility {
    public:
        App20(DEVICES* device);
        void onOpen() override;
        void onRunning() override;
        void onClose() override;

    private:
        DEVICES* _device = nullptr;
        lgfx::LGFX_Sprite* _canvas = nullptr;
        bool _haveCanvas = false;
        char _status[64] = "Press A to run app.elf";
        bool _dirty = true;
        void _present();
    };
}
