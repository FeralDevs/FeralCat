/**
 * @file  app_18.h
 * @brief App18 — Script Runner: list Berry (.be) scripts from /scripts on the
 *        SD card, run the selected one, and show its print() output.
 */
#pragma once
#include <mooncake.h>
#include <LovyanGFX.hpp>
#include "../../bsp/devices.h"

using namespace mooncake;

namespace MOONCAKE::APPS
{
    class App18 : public AppAbility {
    public:
        App18(DEVICES* device);
        void onOpen() override;
        void onRunning() override;
        void onClose() override;

    private:
        DEVICES* _device = nullptr;
        lgfx::LGFX_Sprite* _canvas = nullptr;
        bool _haveCanvas = false;

        enum class Mode { List, Console };
        Mode _mode = Mode::List;

        static constexpr int MAXF = 24;
        char _files[MAXF][40];
        int  _nfiles = 0;
        int  _sel = 0;
        char _title[48] = {0};
        bool _dirty = true;

        void _scan();
        void _runSelected();
        template<typename LCD> void _render(LCD& lcd);
        void _present();
    };
}
