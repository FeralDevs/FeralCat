/**
 * @file  app_19.h
 * @brief App19 — MeowPlayer: an MP3/WAV player for /music on the SD card.
 *
 * Controls: A = play/pause, B = menu (Songs / Output: Speaker/Jack),
 *           Up/Down = volume, Left/Right = seek. Hold B = exit.
 *
 * Threading: a decoder task on core 0 is the ONLY thing that touches the Audio
 * object (no lock contention with the UI). The UI on core 1 posts requests via
 * volatile flags and reads playback state the task publishes back.
 */
#pragma once
#include <mooncake.h>
#include <LovyanGFX.hpp>
#include <Audio.h>
#include "../../bsp/devices.h"

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

        /* Decoder — owned entirely by the core-0 task (see _audioTaskFn). */
        Audio*        _audio     = nullptr;
        TaskHandle_t  _audioTask = nullptr;
        volatile bool _taskRun   = false;
        static void _audioTaskFn(void* arg);
        void _doOpen(int idx);          /* runs IN the task */

        bool _spkReady = false;
        bool _ampOn    = true;          /* PA_EN: speaker vs jack (UI/I2C side) */
        int  _vol      = 10;
        bool _openOk   = false;
        char _diag[80] = {0};

        /* UI → task requests (a value >=0 / true means "pending"). */
        volatile int  _reqSong  = -1;
        volatile bool _reqPause = false;
        volatile int  _reqSeek  = 0;
        volatile int  _reqVol   = -1;
        /* task → UI published state. */
        volatile bool     _running = false;
        volatile uint32_t _pos = 0, _dur = 0;

        enum class Screen { Now, Menu, Songs } _screen = Screen::Now;

        static constexpr int MAXS = 32;
        char _songs[MAXS][48];
        int  _nsongs = 0;
        int  _cur    = -1;

        int  _msel = 0;                 /* top-menu selection            */
        int  _ssel = 0, _stop = 0;      /* song-list selection + scroll  */
        const char* _menuItems[2];
        char _songsItem[16];
        char _outItem[24];
        const char* _songPtrs[MAXS];

        uint32_t _lastDraw = 0;

        int  volMax() const;
        void _applyVol();               /* clamp _vol, post _reqVol */
        bool _initCodec();
        void _setAmp(bool on);
        void _scan();
        void _buildMenu();
        void _present();
    };
}
