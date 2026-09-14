/**
 * @file  app_19.cpp
 * @brief MeowPlayer — see app_19.h.
 *
 * The ES8311 is configured over I2C as an I2S-slave DAC; the ESP32-audioI2S
 * decoder (owned by the core-0 task) streams the file and drives I2S → NS4150B
 * amp (PA_EN) → speaker, or the 3.5mm jack off the same DAC. The UI never calls
 * into Audio directly — it posts request flags the task services between loops.
 */
#include "../../bsp/config.h"
#if MEOWKIT_ENABLE_PLAYER

#include "app_19.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <cstring>
#include "../app_common/mk_tui.h"
#include "meowplayer_ui.h"

namespace MOONCAKE::APPS
{

#define VOL_MAX_JACK  21
#define VOL_MAX_SPK   12      /* protect the 1W speaker (NS4150B can overdrive) */
#define MUSIC_DIR     "/music"

App19::App19(DEVICES* device) : _device(device)
{
    setAppInfo().name = "MeowPlayer";
}

int  App19::volMax() const { return _ampOn ? VOL_MAX_SPK : VOL_MAX_JACK; }

/* UI side: clamp and hand the task a new volume to apply. */
void App19::_applyVol()
{
    if (_vol > volMax()) _vol = volMax();
    if (_vol < 0) _vol = 0;
    _reqVol = _vol;
}

/* UI/I2C side: gate the NS4150B amp (speaker vs jack). Never touched by the task. */
void App19::_setAmp(bool on)
{
    _device->io_exp.digitalWrite(HAL_IOEXP_PA_EN, on ? HIGH : LOW);
    if (on) delay(120);
    _ampOn = on;
    _applyVol();          /* volume ceiling depends on the output */
}

/* ES8311 codec register config only (I2C). Audio owns the I2S bus. MCLK = 128 x
 * fs is driven out on HAL_PIN_I2S_MCLK, so the codec locks to that. */
bool App19::_initCodec()
{
    es8311_set_i2c(&In_I2C);
    es8311_handle_t h = es8311_create((i2c_port_t)0, ES8311_ADDRRES_0);
    if (!h) return false;
    es8311_write_reg(h, 0x00, 0x1F); delay(50);
    es8311_write_reg(h, 0x00, 0x00); delay(10);

    es8311_clock_config_t clk = {};
    clk.mclk_from_mclk_pin = true;
    clk.mclk_frequency     = 128 * 44100;
    clk.sample_frequency   = 44100;
    esp_err_t err = es8311_init(h, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
    if (err != ESP_OK) { es8311_delete(h); return false; }
    es8311_voice_volume_set(h, 180, NULL);
    es8311_microphone_config(h, false);
    es8311_delete(h);
    return true;
}

/* Runs IN the audio task — the only place connecttoFS is called. */
void App19::_doOpen(int idx)
{
    if (!_audio || idx < 0 || idx >= _nsongs) return;
    char path[80];
    snprintf(path, sizeof(path), "%s/%s", MUSIC_DIR, _songs[idx]);
    _audio->stopSong();
    bool ok = _audio->connecttoFS(SD_MMC, path);
    _cur = idx;
    _openOk = ok;
    Serial.printf("[MeowPlayer] open %s: %s\n", path, ok ? "ok" : "FAIL");
}

/* Decoder pump + command service — core 0. loop() is called flat-out (its
 * blocking i2s_write paces playback); we only sleep when nothing is playing. */
void App19::_audioTaskFn(void* arg)
{
    App19* s = (App19*)arg;
    uint32_t n = 0;
    while (s->_taskRun) {
        if (s->_audio) {
            int song = s->_reqSong;
            if (song >= 0) { s->_reqSong = -1; s->_doOpen(song); }
            if (s->_reqPause) { s->_reqPause = false; s->_audio->pauseResume(); }
            int sk = s->_reqSeek; if (sk) { s->_reqSeek = 0; s->_audio->setTimeOffset(sk); }
            int v = s->_reqVol; if (v >= 0) { s->_reqVol = -1; s->_audio->setVolume((uint8_t)v); }

            s->_audio->loop();

            s->_running = s->_audio->isRunning();
            s->_pos = s->_audio->getAudioCurrentTime();
            s->_dur = s->_audio->getAudioFileDuration();
        }
        if (!s->_running)          vTaskDelay(5);
        else if ((++n & 0xFF) == 0) vTaskDelay(1);   /* feed WDT during buffer-fill */
    }
    vTaskDelete(nullptr);
}

void App19::_scan()
{
    _nsongs = 0;
    File dir = SD_MMC.open(MUSIC_DIR);
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return; }
    File f;
    while (_nsongs < MAXS && (f = dir.openNextFile())) {
        if (!f.isDirectory()) {
            const char* nm = f.name();
            const char* base = strrchr(nm, '/'); base = base ? base + 1 : nm;
            size_t L = strlen(base);
            if (L > 4 && (!strcasecmp(base + L - 4, ".mp3") ||
                          !strcasecmp(base + L - 4, ".wav"))) {
                strncpy(_songs[_nsongs], base, sizeof(_songs[0]) - 1);
                _songs[_nsongs][sizeof(_songs[0]) - 1] = '\0';
                _nsongs++;
            }
        }
        f.close();
    }
    dir.close();
    Serial.printf("[MeowPlayer] %d song(s)\n", _nsongs);
}

void App19::_buildMenu()
{
    snprintf(_songsItem, sizeof(_songsItem), "Songs (%d)", _nsongs);
    snprintf(_outItem,   sizeof(_outItem),   "Output: %s", _ampOn ? "Speaker" : "Jack");
    _menuItems[0] = _songsItem;
    _menuItems[1] = _outItem;
    for (int i = 0; i < _nsongs; i++) _songPtrs[i] = _songs[i];
}

void App19::onOpen()
{
    _haveCanvas = false;
    _canvas = new lgfx::LGFX_Sprite(&_device->Lcd);
    if (_canvas) {
        _canvas->setColorDepth(16);
        _canvas->setPsram(true);
        _haveCanvas = _canvas->createSprite(MK_LAYOUT::W, MK_LAYOUT::H);
    }

    _device->sd.begin();
    _spkReady = _initCodec();
    Serial.printf("[MeowPlayer] codec init: %s\n", _spkReady ? "OK" : "FAIL");

    _scan();
    _buildMenu();
    _screen  = Screen::Now;
    _lastDraw = 0;

    if (_spkReady) {
        _audio = new Audio(false, 3, I2S_NUM_0);
        _audio->setPinout(BCLKPIN, WSPIN, DOPIN, HAL_PIN_I2S_MCLK);
        _reqVol = _vol;
        _taskRun = true;
        xTaskCreatePinnedToCore(_audioTaskFn, "mp3", 4096, this, 4, &_audioTask, 0);
        if (_nsongs > 0) { _setAmp(true); _reqSong = 0; }   /* auto-play first track */
    }
}

void App19::_present()
{
    if (_screen == Screen::Menu || _screen == Screen::Songs) {
        MeowPlayer::MenuView m;
        if (_screen == Screen::Menu) {
            m.title = "Menu"; m.items = _menuItems; m.count = 2; m.sel = _msel; m.top = 0;
        } else {
            m.title = "Songs"; m.items = _songPtrs; m.count = _nsongs; m.sel = _ssel; m.top = _stop;
        }
        if (_haveCanvas) { MeowPlayer::drawMenu(*_canvas, m); _canvas->pushSprite(&_device->Lcd, 0, 0); }
        else             MeowPlayer::drawMenu(_device->Lcd, m);
        return;
    }

    MeowPlayer::View v;
    v.spkReady = _spkReady;
    v.ampOn    = _ampOn;
    v.playing  = _running;
    v.title    = (_cur >= 0 && _cur < _nsongs) ? _songs[_cur] : "";
    v.posSec   = (int)_pos;
    v.durSec   = (int)_dur;
    v.vol      = _vol;
    v.volMax   = volMax();
    v.diag     = _diag;
    if (_haveCanvas) { MeowPlayer::drawNowPlaying(*_canvas, v); _canvas->pushSprite(&_device->Lcd, 0, 0); }
    else             MeowPlayer::drawNowPlaying(_device->Lcd, v);
}

void App19::onRunning()
{
    _device->button.update();
    _device->button.tick();

    const bool a  = _device->button.A.pressed();
    const bool b  = _device->button.B.pressed();
    const bool up = _device->button.Up.pressed();
    const bool dn = _device->button.Down.pressed();
    const bool lf = _device->button.Left.pressed();
    const bool rt = _device->button.Right.pressed();
    const bool acted = a || b || up || dn || lf || rt;

    if (_screen == Screen::Now) {
        if (a)  _reqPause = true;
        if (b)  { _buildMenu(); _msel = 0; _screen = Screen::Menu; }
        if (up) { _vol++; _applyVol(); }
        if (dn) { _vol--; _applyVol(); }
        if (lf) _reqSeek -= 5;
        if (rt) _reqSeek += 5;
    } else if (_screen == Screen::Menu) {
        if (up && _msel > 0) _msel--;
        if (dn && _msel < 1) _msel++;
        if (a) {
            if (_msel == 0) { _ssel = (_cur >= 0) ? _cur : 0; _stop = 0; _screen = Screen::Songs; }
            else            { _setAmp(!_ampOn); _buildMenu(); }
        }
        if (b) _screen = Screen::Now;
    } else { /* Songs */
        if (up && _ssel > 0) _ssel--;
        if (dn && _ssel < _nsongs - 1) _ssel++;
        if (_ssel < _stop) _stop = _ssel;
        if (_ssel > _stop + 7) _stop = _ssel - 7;
        if (a && _nsongs > 0) { _setAmp(_ampOn); _reqSong = _ssel; _screen = Screen::Now; }
        if (b) _screen = Screen::Menu;
    }

    /* Diagnostics line (bottom of Now-Playing). */
    {
        static uint32_t lastLog = 0;
        if (millis() - lastLog > 500) {
            lastLog = millis();
            snprintf(_diag, sizeof(_diag), "cdc%d opn%d run%d %lu/%lus fh%u",
                     (int)_spkReady, (int)_openOk, (int)_running,
                     (unsigned long)_pos, (unsigned long)_dur,
                     (unsigned)ESP.getFreeHeap() / 1024);
        }
    }

    uint32_t now = millis();
    if (acted || now - _lastDraw >= 300) { _present(); _lastDraw = now; }
}

void App19::onClose()
{
    _taskRun = false;
    delay(60);                 /* task self-deletes after seeing the flag */
    _audioTask = nullptr;
    if (_audio) { _audio->stopSong(); delete _audio; _audio = nullptr; }
    _setAmp(false);
    if (_canvas) { _canvas->deleteSprite(); delete _canvas; _canvas = nullptr; }
    _haveCanvas = false;
    _running = false;
}

} // namespace MOONCAKE::APPS

#endif // MEOWKIT_ENABLE_PLAYER
