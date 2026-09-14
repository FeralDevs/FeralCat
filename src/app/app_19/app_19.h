/**
 * @file  app_19.h
 * @brief App19 — MeowPlayer: native MP3 player over the meow::media AudioService
 *        (audio backend harvested from PR #2; no Lua runtime).
 *
 * Controls: A = play/pause, B = menu (Songs / Output: Speaker/Jack),
 *           Up/Down = volume, Left/Right = seek. Hold B = exit.
 *
 * All SD access, MP3 decoding and I2S live in the AudioService worker (core 0).
 * This app only sends Commands and renders the Status it publishes back.
 */
#pragma once
#include <mooncake.h>
#include <LovyanGFX.hpp>
#include "../../bsp/devices.h"
#include "../../system/media/audio_service.h"

using namespace mooncake;

namespace MOONCAKE::APPS
{
    class App19 : public AppAbility {
    public:
        App19(DEVICES* device);
        void onOpen() override;
        void onRunning() override;
        void onClose() override;

    private:
        DEVICES* _device = nullptr;
        lgfx::LGFX_Sprite* _canvas = nullptr;
        bool _haveCanvas = false;

        meow::media::AudioService _svc;
        bool _svcReady = false;
        bool _ampOn    = true;      /* PA_EN: speaker (service default) vs jack */

        enum class Screen { Now, Menu, Songs } _screen = Screen::Now;

        static constexpr int MAXT = 64;
        uint16_t    _ids[MAXT];
        char        _titles[MAXT][64];
        const char* _titlePtrs[MAXT];
        int  _ntracks = 0;
        uint32_t _cachedGen = 0xFFFFFFFF;   /* catalog generation we cached  */
        int  _cur = -1;                     /* index of the playing track    */
        uint32_t _lastFinished = 0;         /* for auto-advance              */

        int  _msel = 0;                     /* top-menu selection            */
        int  _ssel = 0, _stop = 0;          /* song-list selection + scroll  */
        const char* _menuItems[2];
        char _songsItem[16];
        char _outItem[24];

        meow::media::Status _st{};
        uint32_t _lastPoll = 0, _lastDraw = 0;
        char _diag[80] = {0};

        void _refreshCatalog();
        void _play(int idx);
        void _setAmp(bool on);
        void _buildMenu();
        void _present();
    };
}
