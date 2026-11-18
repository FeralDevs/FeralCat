#include "system_sound.h"
#include "system_sound_assets.h"
#include "../bsp/devices.h"
#include "../bsp/config.h"
#include "../../lib/ESP32-audioI2S/src/mp3_decoder/mp3_decoder.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace {
enum class SoundId : uint8_t { Stop, Boot, Button };
struct SoundCommand { SoundId id; uint32_t limit_ms; };
DEVICES* s_dev = nullptr;
QueueHandle_t s_queue = nullptr;
TaskHandle_t s_task = nullptr;
/* Codec ownership. s_lock is held by the sound task for the whole of a
 * playback and by suspend/resume while they hand the ES8311/I2S over, so the
 * codec is never released (or re-begun) while a tone is still writing to I2S.
 * s_depth counts outstanding suspends (VU Meter, MeowPlayer, Tracker beep,
 * NES); the codec is only reclaimed when the last one resumes. */
SemaphoreHandle_t s_lock = nullptr;
std::atomic<int> s_depth{0};
volatile bool s_codec_owned = false;
volatile bool s_stop = false;
volatile bool s_ready = false;
volatile bool s_key_enabled = true;
volatile int s_volume = 50;

size_t skip_id3(const uint8_t* p, size_t n) {
    if (n < 10 || p[0] != 'I' || p[1] != 'D' || p[2] != '3') return 0;
    size_t z = ((size_t)(p[6] & 0x7f) << 21) | ((size_t)(p[7] & 0x7f) << 14) |
               ((size_t)(p[8] & 0x7f) << 7) | (size_t)(p[9] & 0x7f);
    z += 10;
    return z < n ? z : n;
}

void amp(bool on) {
    if (!s_dev) return;
    if (on) {
        s_dev->speaker.setMute(true);
        s_dev->io_exp.digitalWrite(HAL_IOEXP_PA_EN, HIGH);
        delay(12);
        s_dev->speaker.setMute(false);
    } else {
        s_dev->speaker.setMute(true);
        delay(4);
        s_dev->io_exp.digitalWrite(HAL_IOEXP_PA_EN, LOW);
    }
}

constexpr TickType_t HandoverTimeout = pdMS_TO_TICKS(1000);

/* Caller holds s_lock. */
bool acquire_codec() {
    if (s_codec_owned) return true;
    s_dev->speaker.config().sample_rate = 44100;
    if (!s_dev->speaker.begin(&In_I2C)) return false;
    s_dev->speaker.setVolume(s_volume * SPK_VOLUME_MAX / 100);
    s_dev->speaker.setMute(true);
    s_dev->io_exp.digitalWrite(HAL_IOEXP_PA_EN, LOW);
    s_codec_owned = true;
    return true;
}

/* Caller holds s_lock. */
void release_codec() {
    if (!s_codec_owned) return;
    s_dev->speaker.setMute(true);
    s_dev->io_exp.digitalWrite(HAL_IOEXP_PA_EN, LOW);
    s_dev->speaker.end();
    s_codec_owned = false;
}

/* Caller holds s_lock and owns the codec. */
bool play_mp3(const uint8_t* data, size_t len, uint32_t limit_ms) {
    if (!data || len < 4 || s_volume == 0) return false;
    if (!MP3Decoder_AllocateBuffers()) return false;
    MP3Decoder_ClearBuffer();
    int16_t* pcm = static_cast<int16_t*>(heap_caps_malloc(
        2304 * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!pcm) { MP3Decoder_FreeBuffers(); return false; }

    s_stop = false;
    amp(true);
    uint32_t deadline = limit_ms ? millis() + limit_ms : 0;
    size_t off = skip_id3(data, len);
    bool played = false;
    while (!s_stop && off + 4 < len) {
        if (deadline && (int32_t)(millis() - deadline) >= 0) break;
        int sync = MP3FindSyncWord(const_cast<unsigned char*>(data + off),
                                   static_cast<int>(len - off));
        if (sync < 0) break;
        off += (size_t)sync;
        int before = (int)(len - off);
        int left = before;
        int err = MP3Decode(const_cast<unsigned char*>(data + off), &left, pcm, 0);
        int used = before - left;
        if (used <= 0) { ++off; continue; }
        off += (size_t)used;
        if (err == 0) {
            int rate = MP3GetSampRate();
            int channels = MP3GetChannels();
            int samples = MP3GetOutputSamps();
            if (rate > 0 && samples > 0 && (channels == 1 || channels == 2)) {
                if (!s_dev->speaker.playPcm(pcm, (size_t)samples,
                                            (uint8_t)channels, (uint32_t)rate)) break;
                played = true;
            }
        }
        taskYIELD();
    }
    s_dev->speaker.stop();
    amp(false);
    free(pcm);
    MP3Decoder_FreeBuffers();
    return played;
}

void task(void*) {
    SoundCommand c{};
    for (;;) {
        if (xQueueReceive(s_queue, &c, portMAX_DELAY) != pdTRUE) continue;
        if (c.id == SoundId::Stop) { s_stop = true; continue; }
        if (s_depth.load() > 0) continue;
        if (c.id == SoundId::Button && !s_key_enabled) continue;
        if (xSemaphoreTake(s_lock, HandoverTimeout) != pdTRUE) continue;
        /* Re-check under the lock: a suspend may have won the race. If an
         * earlier resume could not re-begin the codec, retry it here. */
        if (s_depth.load() == 0 && acquire_codec())
            play_mp3(c.id == SoundId::Boot ? boot_sound_mp3_data : button_sound_mp3_data,
                     c.id == SoundId::Boot ? boot_sound_mp3_len : button_sound_mp3_len,
                     c.limit_ms);
        xSemaphoreGive(s_lock);
    }
}

void enqueue(SoundId id, uint32_t limit) {
    if (!s_ready || !s_queue || (s_depth.load() > 0 && id != SoundId::Stop)) return;
    s_stop = true;
    SoundCommand c{id, limit};
    xQueueOverwrite(s_queue, &c);
}
}

bool system_sound_init(DEVICES* dev) {
    if (s_ready) return true;
    if (!dev) return false;
    s_dev = dev;
    s_dev->speaker.config().sample_rate = 44100;
    if (!s_dev->speaker.begin(&In_I2C)) return false;
    s_dev->speaker.setMute(true);
    s_dev->io_exp.digitalWrite(HAL_IOEXP_PA_EN, LOW);
    s_codec_owned = true;
    s_queue = xQueueCreate(1, sizeof(SoundCommand));
    s_lock = xSemaphoreCreateMutex();
    if (!s_queue || !s_lock) {
        if (s_queue) vQueueDelete(s_queue);
        if (s_lock) vSemaphoreDelete(s_lock);
        s_queue = nullptr; s_lock = nullptr;
        s_dev->speaker.end(); s_codec_owned = false; return false;
    }
    s_ready = true;
    if (xTaskCreatePinnedToCore(task, "system_sound", 6144, nullptr, 3,
                                &s_task, 1) != pdPASS) {
        s_ready = false; vQueueDelete(s_queue); s_queue = nullptr;
        vSemaphoreDelete(s_lock); s_lock = nullptr;
        s_dev->speaker.end(); s_codec_owned = false; return false;
    }
    return true;
}
void system_sound_set_volume(int p) {
    if (p < 0) p = 0; if (p > 100) p = 100; s_volume = p;
    if (s_dev && s_codec_owned && s_depth.load() == 0 && s_dev->speaker.isEnabled())
        s_dev->speaker.setVolume(p * SPK_VOLUME_MAX / 100);
}
void system_sound_set_key_enabled(bool on) { s_key_enabled = on; }
void system_sound_play_boot(uint32_t ms) { enqueue(SoundId::Boot, ms); }
void system_sound_play_button(void) { enqueue(SoundId::Button, 0); }
void system_sound_stop(void) { enqueue(SoundId::Stop, 0); }

bool system_sound_suspend(void) {
    if (!s_ready) return true;        /* never brought the codec up: nothing to hand over */
    ++s_depth;                        /* from here on no new tone can start */
    s_stop = true;
    if (s_queue) xQueueReset(s_queue);

    /* The sound task holds s_lock for a whole playback; once we have it the
     * decoder is no longer touching I2S and the codec can be released. */
    if (xSemaphoreTake(s_lock, HandoverTimeout) != pdTRUE) {
        --s_depth;                    /* not handed over: caller must not take the codec */
        return false;
    }
    release_codec();
    xSemaphoreGive(s_lock);
    return true;
}

bool system_sound_resume(void) {
    if (!s_ready) return true;
    if (xSemaphoreTake(s_lock, HandoverTimeout) != pdTRUE) {
        /* Cannot happen while suspended (the task does not play), but never
         * leak a suspend: the task re-begins the codec on the next tone. */
        if (s_depth.load() > 0) --s_depth;
        return false;
    }
    bool ok = true;
    if (s_depth.load() > 0 && --s_depth == 0) {
        s_stop = false;
        /* On failure system sound is not lost for good: the task retries
         * acquire_codec() before the next tone. */
        ok = acquire_codec();
    }
    xSemaphoreGive(s_lock);
    return ok;
}
