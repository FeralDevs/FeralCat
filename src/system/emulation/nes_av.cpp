#include "nes_av.h"

#ifdef MEOW_NES_AV_HOST_TEST
#include "nes_av_test_platform.h"
#else
#include "../../bsp/devices.h"
#include <Arduino.h>
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstring>
#include <new>

namespace meow::nes {
namespace {
constexpr size_t StripRows = 8;
constexpr size_t StripPixels = Av::Width * StripRows;
constexpr size_t AudioBlocks = 3;
constexpr size_t AudioSamples = 1024;
constexpr size_t DmaFrames = 256;
constexpr size_t DmaBuffers = 8;
constexpr size_t PrefillSamples = Av::SampleRate / 30; // Two NTSC frames.
constexpr uint32_t WriteWaitMs = 10;
constexpr uint32_t NoProgressMs = 100;
constexpr int64_t DrainUs = DmaFrames * DmaBuffers * 1000000LL / Av::SampleRate;

TickType_t ticks(uint32_t ms)
{
    return ms ? std::max<TickType_t>(1, pdMS_TO_TICKS(ms)) : 0;
}

uint16_t wire565(uint16_t color)
{
    return static_cast<uint16_t>((color << 8) | (color >> 8));
}
} // namespace

bool audioAllowsDisplay(bool prefilling, size_t pendingSamples, size_t frameSamples,
                        uint32_t coreUs, uint32_t drawUs, uint32_t sinceLastDrawMs)
{
    if (prefilling || !frameSamples) return false;
    if (sinceLastDrawMs >= 250) return true;
    const uint64_t workUs = uint64_t(coreUs) + drawUs + 1000;
    const uint64_t estimate = (workUs * Av::SampleRate + 999999) / 1000000;
    // Each software block holds at most AudioSamples. A spurious timing
    // outlier must never demand more reserve than the producer can build.
    const size_t reserve = static_cast<size_t>(std::min<uint64_t>(
        estimate, 2 * std::min(frameSamples, AudioSamples)));
    return pendingSamples >= reserve;
}

struct Av::Impl {
    struct AudioBlock {
        int16_t mono[AudioSamples];
        size_t count;
    };

    DEVICES* device = nullptr;
    uint16_t* strips[2]{};
    uint8_t previousDepth = 16;
    bool depthChanged = false;
    bool audio = false;
    bool codec = false;
    bool i2sInstalled = false;
    speaker_config_t previousSpeaker{};
    QueueHandle_t empty = nullptr;
    QueueHandle_t full = nullptr;
    SemaphoreHandle_t done = nullptr;
    TaskHandle_t worker = nullptr;
    AudioBlock pool[AudioBlocks]{};
    int16_t stereo[AudioSamples * 2]{};
    std::atomic<bool> quitting{false};
    std::atomic<bool> failed{false};
    std::atomic<unsigned> volume{100};
    std::atomic<uint32_t> written{0};
    std::atomic<uint32_t> writeTimeouts{0};
    std::atomic<uint32_t> audioErrors{0};
    std::atomic<uint32_t> underruns{0};
    std::atomic<size_t> pending{0};
    std::atomic<unsigned> queuedBlocks{0};
    std::atomic<bool> prefilling{true};
    AvStats ownerStats{};

    AvStats snapshot() const
    {
        AvStats result = ownerStats;
        result.audioSamplesWritten = written.load();
        result.audioWriteTimeouts = writeTimeouts.load();
        result.audioErrors = audioErrors.load();
        result.audioUnderruns = underruns.load();
        return result;
    }

    static void audioEntry(void* context)
    {
        auto* self = static_cast<Impl*>(context);
        bool hadAudio = false;
        bool starved = false;
        int64_t lastWrite = esp_timer_get_time();
        while (!self->quitting.load()) {
            // A software reserve avoids feeding isolated frame-sized bursts to
            // a running DMA ring. Full-pool escape also supports small blocks.
            if (self->prefilling.load()) {
                if (self->pending.load() < PrefillSamples &&
                    self->queuedBlocks.load() < AudioBlocks) {
                    vTaskDelay(1);
                    continue;
                }
                self->prefilling.store(false);
            }
            uint8_t index = 0;
            if (xQueueReceive(self->full, &index, ticks(WriteWaitMs)) != pdTRUE) {
                // DMA can still be draining after the software queue empties.
                // Count one incident only after that entire capacity elapsed.
                if (hadAudio && !starved &&
                    esp_timer_get_time() - lastWrite > DrainUs + WriteWaitMs * 1000) {
                    self->underruns.fetch_add(1);
                    starved = true;
                    self->prefilling.store(true);
                }
                continue;
            }
            self->queuedBlocks.fetch_sub(1);
            if (self->quitting.load()) break;
            const AudioBlock& block = self->pool[index];
            const int32_t volume = static_cast<int32_t>(self->volume.load(std::memory_order_relaxed));
            for (size_t i = 0; i < block.count; ++i) {
                const int16_t sample = static_cast<int16_t>(static_cast<int32_t>(block.mono[i]) * volume / 100);
                self->stereo[2 * i] = sample;
                self->stereo[2 * i + 1] = sample;
            }
            const size_t bytes = block.count * 2 * sizeof(int16_t);
            size_t offset = 0;
            int64_t progress = esp_timer_get_time();
            while (offset < bytes && !self->quitting.load()) {
                size_t used = 0;
                const esp_err_t result = i2s_write(I2S_NUM_0,
                    reinterpret_cast<const uint8_t*>(self->stereo) + offset,
                    bytes - offset, &used, ticks(WriteWaitMs));
                if (used > bytes - offset || used % (2 * sizeof(int16_t)) != 0 ||
                    (result != ESP_OK && result != ESP_ERR_TIMEOUT)) {
                    self->audioErrors.fetch_add(1);
                    self->failed.store(true);
                    self->quitting.store(true);
                    break;
                }
                if (result == ESP_ERR_TIMEOUT || used == 0) self->writeTimeouts.fetch_add(1);
                if (used) {
                    offset += used;
                    const size_t samples = used / (2 * sizeof(int16_t));
                    self->pending.fetch_sub(samples);
                    self->written.fetch_add(static_cast<uint32_t>(samples));
                    progress = lastWrite = esp_timer_get_time();
                    hadAudio = true;
                    starved = false;
                } else if (esp_timer_get_time() - progress >= NoProgressMs * 1000LL) {
                    self->audioErrors.fetch_add(1);
                    self->failed.store(true);
                    self->quitting.store(true);
                    break;
                } else {
                    // Some drivers report a zero-length write immediately.
                    vTaskDelay(1);
                }
            }
            // There is always one vacant slot for the block held by this worker.
            xQueueSend(self->empty, &index, 0);
        }
        const auto completion = self->done;
        xSemaphoreGive(completion);
        // No access to self, queues, buffers or the codec after acknowledgement.
        vTaskDelete(nullptr);
    }
};

Av::~Av()
{
    // A timed-out worker must retain its heap context and hardware ownership.
    // The session should call stop() explicitly and handle false before leaving.
    if (!stop()) Serial.println("[NES] AV stop timed out; resources retained");
}

void Av::setError(const char* text)
{
    const char* source = text ? text : "";
    const size_t length = std::min(std::strlen(source), sizeof(error_) - 1);
    std::memcpy(error_, source, length);
    error_[length] = 0;
}

void Av::setVolume(unsigned percent)
{
    volume_ = std::min(percent, 100u);
    if (impl_) impl_->volume.store(volume_, std::memory_order_relaxed);
}

bool Av::begin(DEVICES* device, bool enableAudio)
{
    if (impl_) { setError("NES AV session already owns resources"); return false; }
    setError("");
    lastStats_ = {};
    if (!device || device->Lcd.width() != 320 || device->Lcd.height() != Height) {
        setError("NES requires a 320x240 display");
        return false;
    }
    void* storage = heap_caps_malloc(sizeof(Impl), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!storage) { setError("No internal memory for NES AV"); return false; }
    impl_ = new (storage) Impl;
    impl_->volume.store(volume_, std::memory_order_relaxed);
    impl_->device = device;
    impl_->previousDepth = device->Lcd.getColorDepth();
    impl_->previousSpeaker = device->speaker.config();
    auto fail = [&](const char* message) {
        setError(message);
        stop();
        return false;
    };
    for (auto& strip : impl_->strips) {
        strip = static_cast<uint16_t*>(heap_caps_malloc(StripPixels * sizeof(uint16_t),
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
        if (!strip) return fail("No DMA memory for NES video");
    }
    device->Lcd.setColorDepth(16);
    impl_->depthChanged = true;
    if (!enableAudio) return true;

    // The session has stopped all previous owners before this point. Reuse the
    // established ES8311 codec/gain path; this HAL owns the separate I2S driver.
    if (!device->io_exp.digitalWrite(HAL_IOEXP_PA_EN, LOW)) return fail("NES amplifier control failed");
    device->speaker.config().sample_rate = SampleRate;
    device->speaker.config().bits_per_sample = 16;
    impl_->codec = true; // Also clean up a partially initialized codec on failure.
    if (!device->speaker.beginCodecOnly(&In_I2C) ||
        !device->speaker.setPlayerDacAttenuation() || !device->speaker.setMute(false)) {
        return fail("NES codec initialization failed");
    }
    i2s_config_t config{};
    config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
    config.sample_rate = SampleRate;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = DmaBuffers;
    config.dma_buf_len = DmaFrames;
    config.use_apll = false;
    config.tx_desc_auto_clear = true;
    if (i2s_driver_install(I2S_NUM_0, &config, 0, nullptr) != ESP_OK) {
        return fail("NES I2S0 unavailable");
    }
    impl_->i2sInstalled = true;
    i2s_pin_config_t pins{};
    pins.mck_io_num = -1;
    pins.bck_io_num = HAL_PIN_I2S_BCLK;
    pins.ws_io_num = HAL_PIN_I2S_WS;
    pins.data_out_num = HAL_PIN_I2S_DOUT;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    // IDF 4.4's install already starts DMA. Calling start again resets it and
    // invalidates the initial timing. Keep zeroed DMA running while PCM primes;
    // i2s_write needs descriptors returned by its completion interrupts.
    if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK ||
        i2s_zero_dma_buffer(I2S_NUM_0) != ESP_OK) {
        return fail("NES I2S0 configuration failed");
    }
    impl_->empty = xQueueCreate(AudioBlocks, sizeof(uint8_t));
    impl_->full = xQueueCreate(AudioBlocks, sizeof(uint8_t));
    impl_->done = xSemaphoreCreateBinary();
    if (!impl_->empty || !impl_->full || !impl_->done) return fail("No NES audio queue memory");
    for (uint8_t i = 0; i < AudioBlocks; ++i) xQueueSend(impl_->empty, &i, 0);
    if (xTaskCreatePinnedToCore(Impl::audioEntry, "nes_pcm", 4096, impl_, 2,
                              &impl_->worker, 0) != pdPASS) {
        return fail("NES audio task creation failed");
    }
    if (!device->io_exp.digitalWrite(HAL_IOEXP_PA_EN, HIGH)) return fail("NES amplifier enable failed");
    impl_->audio = true;
    return true;
}

bool Av::submitFrame(const uint8_t* indexed, size_t pitch, const uint16_t* palette565)
{
    if (!impl_ || !indexed || !palette565 || pitch < Width || pitch > 4096 ||
        impl_->quitting.load()) {
        setError("Invalid NES frame or stopped session");
        return false;
    }
    const int64_t started = esp_timer_get_time();
    uint16_t palette[256];
    for (size_t i = 0; i < 256; ++i) palette[i] = wire565(palette565[i]);
    auto& lcd = impl_->device->Lcd;
    lcd.startWrite();
    lcd.setAddrWindow(Left, 0, Width, Height);
    unsigned which = 0;
    for (size_t y = 0; y < Height; y += StripRows) {
        uint16_t* out = impl_->strips[which];
        const size_t rows = std::min(StripRows, static_cast<size_t>(Height) - y);
        for (size_t row = 0; row < rows; ++row) {
            const uint8_t* source = indexed + (y + row) * pitch;
            for (size_t x = 0; x < Width; ++x) out[row * Width + x] = palette[source[x]];
        }
        // Convert into the other owned strip while the previous strip is sent.
        // Explicit wire-order RGB565 avoids LovyanGFX's conversion allocation.
        if (lcd.dmaBusy()) ++impl_->ownerStats.dmaWaits;
        lcd.waitDMA();
        lcd.writePixelsDMA(out, rows * Width, false);
        which ^= 1;
    }
    if (lcd.dmaBusy()) ++impl_->ownerStats.dmaWaits;
    lcd.waitDMA();
    lcd.endWrite();
    ++impl_->ownerStats.frames;
    impl_->ownerStats.lastFrameUs = static_cast<uint32_t>(esp_timer_get_time() - started);
    impl_->ownerStats.maxFrameUs = std::max(impl_->ownerStats.maxFrameUs, impl_->ownerStats.lastFrameUs);
    return true;
}

size_t Av::submitAudio(const int16_t* mono, size_t count, uint32_t timeoutMs)
{
    if (impl_ && impl_->failed.load()) setError("NES audio worker failed");
    if (!impl_ || !impl_->audio || !mono || !count || impl_->quitting.load()) return 0;
    const uint32_t budget = std::min(timeoutMs, 100u);
    const int64_t deadline = esp_timer_get_time() + budget * 1000LL;
    size_t accepted = 0;
    while (accepted < count && !impl_->quitting.load()) {
        const int64_t remaining = deadline - esp_timer_get_time();
        const uint32_t waitMs = remaining > 0 ? static_cast<uint32_t>(remaining / 1000) : 0;
        uint8_t index = 0;
        if (xQueueReceive(impl_->empty, &index, ticks(waitMs)) != pdTRUE) {
            ++impl_->ownerStats.audioQueueTimeouts;
            break;
        }
        auto& block = impl_->pool[index];
        block.count = std::min(AudioSamples, count - accepted);
        std::memcpy(block.mono, mono + accepted, block.count * sizeof(int16_t));
        // Publish accounting before the worker can consume this block.
        impl_->pending.fetch_add(block.count);
        impl_->queuedBlocks.fetch_add(1);
        if (xQueueSend(impl_->full, &index, 0) != pdTRUE) {
            impl_->pending.fetch_sub(block.count);
            impl_->queuedBlocks.fetch_sub(1);
            xQueueSend(impl_->empty, &index, 0);
            ++impl_->ownerStats.audioQueueTimeouts;
            break;
        }
        accepted += block.count;
        impl_->ownerStats.audioSamplesQueued += static_cast<uint32_t>(block.count);
        if (accepted < count && esp_timer_get_time() > deadline) break;
    }
    return accepted;
}

size_t Av::audioPendingSamples() const
{
    return impl_ && impl_->audio ? impl_->pending.load() : 0;
}

bool Av::audioPrefilling() const
{
    return impl_ && impl_->audio && !impl_->quitting.load() && impl_->prefilling.load();
}

bool Av::stop(uint32_t timeoutMs)
{
    if (!impl_) return true;
    Impl* current = impl_;
    current->quitting.store(true);
    if (current->worker) {
        if (xSemaphoreTake(current->done, ticks(std::min(timeoutMs, 5000u))) != pdTRUE) {
            ++current->ownerStats.stopTimeouts;
            setError("NES audio still stopping; hardware remains owned");
            return false;
        }
        current->worker = nullptr;
    }
    if (current->codec) {
        current->device->io_exp.digitalWrite(HAL_IOEXP_PA_EN, LOW);
        current->device->speaker.setMute(true);
    }
    if (current->i2sInstalled) {
        i2s_stop(I2S_NUM_0);
        i2s_zero_dma_buffer(I2S_NUM_0);
        i2s_driver_uninstall(I2S_NUM_0);
    }
    if (current->codec) {
        current->device->speaker.end();
        current->device->speaker.config() = current->previousSpeaker;
    }
    if (current->depthChanged) {
        current->device->Lcd.waitDMA();
        current->device->Lcd.setColorDepth(current->previousDepth);
    }
    if (current->empty) vQueueDelete(current->empty);
    if (current->full) vQueueDelete(current->full);
    if (current->done) vSemaphoreDelete(current->done);
    for (auto* strip : current->strips) heap_caps_free(strip);
    lastStats_ = current->snapshot();
    current->~Impl();
    heap_caps_free(current);
    impl_ = nullptr;
    return true;
}

bool Av::active() const { return impl_ != nullptr; }
AvStats Av::stats() const { return impl_ ? impl_->snapshot() : lastStats_; }

} // namespace meow::nes
