/**
 * @file  app_11.h
 * @brief App11 — WiFi Analyzer: scan 2.4GHz, list APs by signal, per-AP detail.
 */
#pragma once
#include <mooncake.h>
#include <LovyanGFX.hpp>
#include "../../bsp/devices.h"
#include "wifi_analyzer_ui.h"

using namespace mooncake;

namespace MOONCAKE::APPS
{
    class App11 : public AppAbility {
    public:
        App11(DEVICES* device);
        void onOpen() override;
        void onRunning() override;
        void onClose() override;

    private:
        DEVICES* _device = nullptr;

        lgfx::LGFX_Sprite* _canvas = nullptr;   /* lazily allocated in onOpen */
        bool _haveCanvas = false;

        static constexpr int MAXROWS = 40;
        WifiAnalyzer::Row _rows[MAXROWS];
        int  _count  = 0;
        int  _sel    = 0;
        int  _scroll = 0;
        bool _scanning = false;
        bool _dirty  = true;

        enum class Page : uint8_t { List, Detail } _page = Page::List;

        void _scan();
        template<typename LCD> void _render(LCD& lcd);
        void _present();
    };
}
