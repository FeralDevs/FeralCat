#include "audio_service.h"
#include "media_catalog.h"
#include "audio_commands.h"
#include "audio_equalizer.h"
#include "cover_loader.h"
#include "../../bsp/devices.h"
#include <Audio.h>
#include <atomic>
#include <cstring>
#include <new>
#include <esp_heap_caps.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace {
template<size_t N> void copyText(char (&target)[N], const char* source)
{
    if (!source) source = "";
    const size_t length = strnlen(source, N - 1);
    std::memcpy(target, source, length);
    target[length] = '\0';
}

struct DecoderObserver {
    virtual void information(const char*) = 0;
    virtual void eof() = 0;
    virtual bool allowSample(uint32_t&) = 0;
    virtual bool cancelRequested() const = 0;
};
// The vendored decoder uses global codec state and global weak callbacks.
// A single session owns it, even if several AudioService objects are created.
std::atomic<bool> sessionOwned{false};
std::atomic<DecoderObserver*> decoderObserver{nullptr};
}

void audio_info(const char* message)
{
    if (auto* observer = decoderObserver.load()) observer->information(message);
#if MEOWKIT_HW_TEST_ENABLE
    Serial.printf("[Audio] %s\n", message ? message : "");
#endif
}

void audio_eof_mp3(const char*)
{
    if (auto* observer = decoderObserver.load()) observer->eof();
}

void audio_process_i2s(uint32_t* sample, bool* continueI2S)
{
    auto* observer = decoderObserver.load();
    *continueI2S = !observer || observer->allowSample(*sample);
}

bool audio_cancelled()
{
    auto* observer = decoderObserver.load();
    return observer && observer->cancelRequested();
}

namespace meow::media {
struct AudioService::Impl final : DecoderObserver {
    struct Queued { Command command; uint32_t generation; };
    DEVICES* devices = nullptr;
    QueueHandle_t commands = nullptr;
    SemaphoreHandle_t snapshots = nullptr;
    SemaphoreHandle_t done = nullptr;
    TaskHandle_t worker = nullptr;
    std::atomic<bool> quitting{false};
    // Only worker writes decoder/live. published/catalog share one short lock.
    Audio* decoder = nullptr;
    MediaCatalog* catalog = nullptr;
    Equalizer equalizer;
    uint16_t* coverPublished = nullptr;
    uint16_t* coverScratch = nullptr;
    uint32_t coverRevision = 0;
    bool coverAvailable = false;
    Status live;
    Status published;
    bool naturalEnd = false;
    bool hasSamples = false;
    bool unsupportedRate = false;
    bool draining = false;
    uint32_t drainAt = 0;
    uint32_t drainMs = 0;
    char decoderError[160]{};
    uint32_t progressAt = 0;
    uint32_t lastFilePosition = 0;
    uint32_t lastPosition = 0;
    uint32_t publishedAt = 0;

    void information(const char* message) override
    {
        if (!message) return;
        // The library also calls its EOF callback for corrupt files. Preserve
        // that distinction so a playlist never advances after a decode error.
        // Match decoder-owned prefixes only: a legitimate filename such as
        // /mp3/error.mp3 must never turn a successful EOF into an error.
        if (!std::strncmp(message, "MP3 decode error ", 17) ||
            !std::strncmp(message, "MP3 seek error:", 15) ||
            !std::strncmp(message, "Audio read error:", 17) ||
            !std::strncmp(message, "audio file is corrupt", 21) ||
            !std::strcmp(message, "Failed to open file for reading") ||
            !std::strncmp(message, "Out of memory", 13)) {
            copyText(decoderError, message);
        }
    }

    void eof() override { naturalEnd = true; }
    bool cancelRequested() const override { return quitting.load(); }

    bool allowSample(uint32_t& sample) override
    {
        if (quitting.load() || !decoder) return false;
        // forceMono mixes PCM but keeps RIGHT_LEFT I2S: 2 slots x 16 bits.
        // These four SCLK=32*FS entries share the ES8311 44.1kHz divider set.
        const uint32_t rate = decoder->getSampleRate();
        if (rate != 22050 && rate != 32000 && rate != 44100 && rate != 48000) {
            unsupportedRate = true;
            return false;
        }
        hasSamples = true;
        equalizer.setSampleRate(rate);
        equalizer.processWord(sample);
        return true;
    }

    static bool cancelled(void* context)
    {
        return static_cast<Impl*>(context)->quitting.load();
    }

    void publish()
    {
        xSemaphoreTake(snapshots, portMAX_DELAY);
        published = live;
        xSemaphoreGive(snapshots);
        publishedAt = millis();
    }

    void setState(const char* value) { copyText(live.state, value); }

    void publishCover(bool available)
    {
        xSemaphoreTake(snapshots, portMAX_DELAY);
        if (available) {
            auto* previous = coverPublished;
            coverPublished = coverScratch;
            coverScratch = previous;
        }
        coverAvailable = available;
        ++coverRevision;
        xSemaphoreGive(snapshots);
    }

    void halt()
    {
        if (decoder) decoder->stopSong();
        // stopSong clears the software buffer; flush queued PCM as well.
        if (decoder && decoder->isInitialized()) i2s_zero_dma_buffer(I2S_NUM_0);
        naturalEnd = false;
        draining = false;
        equalizer.reset();
    }

    void error(const char* message)
    {
        halt();
        copyText(live.error, message);
        setState("error");
        publish();
    }

    bool ensureDecoder()
    {
        if (decoder) return true;
        decoder = new (std::nothrow) Audio(false, 3, I2S_NUM_0);
        if (!decoder || !decoder->isInitialized()) {
            delete decoder;
            decoder = nullptr;
            error("Could not initialize audio memory or the I2S driver.");
            return false;
        }
        decoder->setBufsize(8192, 65536);
        if (!decoder->setPinout(HAL_PIN_I2S_BCLK, HAL_PIN_I2S_WS, HAL_PIN_I2S_DOUT)) {
            delete decoder;
            decoder = nullptr;
            error("Could not initialize the I2S audio pins.");
            return false;
        }
        decoder->forceMono(true);
        decoder->setVolumeSteps(100);
        decoder->setVolume(live.volume);
        return true;
    }

    void scan()
    {
        halt();
        publishCover(false);
        live.track = 0;
        live.title[0] = live.error[0] = '\0';
        live.position = live.duration = 0;
        setState("scanning");
        publish();
        if (quitting.load()) return;
        // The launcher owns mounting. Its direct SD_MMC.begin() need not be
        // reflected in SDMMC_Class's cached state; never remount from worker.
        auto root = SD_MMC.open("/");
        const bool cardAvailable = root && root.isDirectory();
        root.close();
        if (!cardAvailable) {
            error("SD card is unavailable.");
            return;
        }
        auto* next = new (std::nothrow) MediaCatalog;
        if (!next) { error("Not enough memory for the music library."); return; }
        const auto result = next->scan(SD_MMC, cancelled, this);
        if (result != MediaCatalog::ScanResult::Complete) {
            if (result != MediaCatalog::ScanResult::Cancelled) error(next->error());
            delete next;
            return;
        }
        if (quitting.load()) { delete next; return; }
        live.tracks = next->trackCount();
        live.playlists = next->playlistCount();
        live.skipped = next->skipped();
        ++live.generation;
        setState("idle");
        // Swap catalog and generation together; no filesystem work while locked.
        xSemaphoreTake(snapshots, portMAX_DELAY);
        auto* previous = catalog;
        catalog = next;
        published = live;
        xSemaphoreGive(snapshots);
        delete previous;
        publishedAt = millis();
    }

    void play(const Queued& request)
    {
        if (request.generation != live.generation) {
            error("The music library has changed. Please select the track again.");
            return;
        }
        char path[MaxMediaPath + 1]{};
        char title[96]{};
        // catalog is immutable and exclusively replaced by this worker.
        if (!catalog || !catalog->track(static_cast<uint16_t>(request.command.value),
                path, sizeof(path), title, sizeof(title))) {
            error("The track is not in the current music library.");
            return;
        }
        halt();
        publishCover(false);
        ++live.playback;
        live.track = static_cast<uint16_t>(request.command.value);
        copyText(live.title, title);
        live.error[0] = decoderError[0] = '\0';
        live.position = live.duration = 0;
        hasSamples = unsupportedRate = naturalEnd = false;
        setState("loading");
        publish();
        if (!ensureDecoder() || quitting.load()) return;
        // No JPEG/filesystem work happens in Lua or while music is running.
        // The candidate buffer is private until a short pointer-swap publish.
        if (coverScratch) {
            CoverInfo info{};
            CoverHooks hooks{};
            hooks.cancel = cancelled;
            hooks.context = this;
            if (loadTrackCover(SD_MMC, path, coverScratch, CoverPixels, info, hooks) == CoverResult::Ready)
                publishCover(true);
        }
        if (quitting.load()) return;
        if (!decoder->connecttoFS(SD_MMC, path)) {
            error(decoderError[0] ? decoderError : "Could not open or decode the MP3 file.");
            return;
        }
        decoder->forceMono(true);
        decoder->setVolume(live.volume);
        progressAt = millis();
        lastFilePosition = lastPosition = 0;
        // State stays loading until the decoder has produced real PCM.
    }

    void execute(const Queued& request)
    {
        switch (request.command.action) {
        case Action::Scan: scan(); break;
        case Action::Play: play(request); break;
        case Action::Pause:
            if (decoder && !draining && (!std::strcmp(live.state, "playing") || !std::strcmp(live.state, "paused"))) {
                if (!decoder->pauseResume()) { error("Could not pause or resume playback."); break; }
                if (!decoder->isRunning()) i2s_zero_dma_buffer(I2S_NUM_0);
                setState(decoder->isRunning() ? "playing" : "paused");
                progressAt = millis();
                publish();
            }
            break;
        case Action::Stop:
            halt();
            setState("stopped");
            live.position = 0;
            live.error[0] = '\0';
            publish();
            break;
        case Action::Volume:
            live.volume = static_cast<uint8_t>(request.command.value);
            if (decoder) decoder->setVolume(live.volume);
            publish();
            break;
        case Action::Equalizer:
            if (equalizer.setPreset(request.command.value)) {
                live.equalizer = static_cast<uint8_t>(request.command.value);
                publish();
            }
            break;
        case Action::Seek:
            if (!decoder || draining || (std::strcmp(live.state, "playing") != 0 &&
                                        std::strcmp(live.state, "paused") != 0) ||
                !live.duration || !decoder->setAudioPlayPosition(static_cast<uint16_t>(request.command.value))) {
                error("Seeking is not yet available for this track.");
                break;
            }
            i2s_zero_dma_buffer(I2S_NUM_0);
            equalizer.reset();
            progressAt = millis();
            break;
        }
    }

    void pump()
    {
        if (!decoder || (std::strcmp(live.state, "playing") && std::strcmp(live.state, "loading"))) return;
        if (draining) {
            if (millis() - drainAt < drainMs) return;
            draining = false;
            ++live.finished;
            setState("ended");
            publish();
            return;
        }
        decoder->loop();
        if (quitting.load()) return;
        if (unsupportedRate) {
            error("Unsupported MP3 sample rate. Supported: 22.05, 32, 44.1 and 48 kHz.");
            return;
        }
        if (naturalEnd) {
            naturalEnd = false;
            if (decoderError[0] || !hasSamples) {
                error(decoderError[0] ? decoderError : "The MP3 file contains no playable audio.");
            } else {
                live.position = decoder->getAudioCurrentTime();
                if (live.duration && live.position < live.duration) live.position = live.duration;
                // EOF means PCM has been queued, not that the final samples
                // have left DMA. Allow all 16 x 512 stereo frames to drain
                // before notifying Lua; advancing early would truncate tails.
                const uint32_t rate = decoder->getSampleRate();
                drainMs = (16u * 512u * 1000u + rate - 1u) / rate + 2u;
                drainAt = millis();
                draining = true;
            }
            return;
        }
        if (!decoder->isRunning()) {
            error(decoderError[0] ? decoderError : "MP3 playback stopped because of a read error.");
            return;
        }
        if (hasSamples && !std::strcmp(live.state, "loading")) {
            setState("playing");
            publish();
        }
        const uint32_t now = millis();
        const uint32_t position = decoder->getAudioCurrentTime();
        const uint32_t filePosition = decoder->getFilePos();
        if (position != lastPosition || filePosition != lastFilePosition) {
            lastPosition = position;
            lastFilePosition = filePosition;
            progressAt = now;
        } else if (now - progressAt > 10000) {
            error("MP3 playback has stalled. Check the SD card and file.");
            return;
        }
        live.position = position;
        const uint32_t duration = decoder->getAudioFileDuration();
        if (duration) live.duration = duration;
        if (now - publishedAt >= 100) publish();
    }

    static void entry(void* context)
    {
        auto* self = static_cast<Impl*>(context);
        decoderObserver.store(self);
        self->scan();
        while (!self->quitting.load()) {
            Queued request{};
            if (xQueueReceive(self->commands, &request, 0) == pdTRUE) {
                coalesceVolume(request,
                    [&](Queued& next) { return xQueuePeek(self->commands, &next, 0) == pdTRUE; },
                    [&](Queued& next) { return xQueueReceive(self->commands, &next, 0) == pdTRUE; });
                self->execute(request);
            }
            if (self->quitting.load()) break;
            self->pump();
            // I2S writes use zero timeout; yield between pump iterations.
            vTaskDelay(1);
        }
        self->halt();
        delete self->decoder;
        self->decoder = nullptr;
        decoderObserver.store(nullptr);
        const auto completion = self->done;
        xSemaphoreGive(completion);
        vTaskDelete(nullptr); // self-delete only after every file/I2S handle is closed
    }
};

AudioService::~AudioService() { end(); }

bool AudioService::begin(DEVICES* devices)
{
    if (impl_) return ready();
    bool expected = false;
    if (!devices || !sessionOwned.compare_exchange_strong(expected, true)) return false;
    impl_ = new (std::nothrow) Impl;
    if (!impl_) { sessionOwned.store(false); return false; }
    impl_->devices = devices;
    impl_->commands = xQueueCreate(8, sizeof(Impl::Queued));
    impl_->snapshots = xSemaphoreCreateMutex();
    impl_->done = xSemaphoreCreateBinary();
    if (!impl_->commands || !impl_->snapshots || !impl_->done) { end(); return false; }
    impl_->coverPublished = static_cast<uint16_t*>(heap_caps_malloc(CoverPixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    impl_->coverScratch = static_cast<uint16_t*>(heap_caps_malloc(CoverPixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!impl_->coverPublished || !impl_->coverScratch) {
        heap_caps_free(impl_->coverPublished);
        heap_caps_free(impl_->coverScratch);
        impl_->coverPublished = impl_->coverScratch = nullptr; // Artwork is optional.
    }
    // Shared BCLK/WS cannot have microphone and speaker masters simultaneously.
    devices->mic.end();
    devices->io_exp.digitalWrite(HAL_IOEXP_PA_EN, LOW);
    devices->speaker.end();
    devices->speaker.config().sample_rate = 44100;
    devices->speaker.config().bits_per_sample = 16;
    if (!devices->speaker.beginCodecOnly(&In_I2C) ||
        !devices->speaker.setPlayerDacAttenuation() || !devices->speaker.setMute(false)) {
        end();
        return false;
    }
    copyText(impl_->published.state, "scanning");
    if (xTaskCreatePinnedToCore(Impl::entry, "meow_mp3", 12288, impl_, 2, &impl_->worker, 0) != pdPASS) {
        end();
        return false;
    }
    if (!devices->io_exp.digitalWrite(HAL_IOEXP_PA_EN, HIGH)) { end(); return false; }
    return true;
}

void AudioService::end()
{
    if (!impl_) return;
    impl_->quitting.store(true);
    if (impl_->worker) {
        // Scan checks quitting between directory entries/playlist lines. Any
        // in-flight SD read finishes normally before the completion signal.
        while (xSemaphoreTake(impl_->done, pdMS_TO_TICKS(50)) != pdTRUE) { delay(1); }
    }
    if (impl_->devices) {
        impl_->devices->io_exp.digitalWrite(HAL_IOEXP_PA_EN, LOW);
        impl_->devices->speaker.end();
    }
    delete impl_->catalog;
    heap_caps_free(impl_->coverPublished);
    heap_caps_free(impl_->coverScratch);
    if (impl_->commands) vQueueDelete(impl_->commands);
    if (impl_->snapshots) vSemaphoreDelete(impl_->snapshots);
    if (impl_->done) vSemaphoreDelete(impl_->done);
    delete impl_;
    impl_ = nullptr;
    sessionOwned.store(false);
}

bool AudioService::ready() const
{
    return impl_ && impl_->worker && !impl_->quitting.load();
}

bool AudioService::command(Command command)
{
    if (!ready()) return false;
    switch (command.action) {
    case Action::Play: if (command.value < 1 || command.value > static_cast<int32_t>(MaxTracks)) return false; break;
    case Action::Volume: if (command.value < 0 || command.value > 100) return false; break;
    case Action::Equalizer: if (!Equalizer::validPreset(command.value)) return false; break;
    case Action::Seek: if (command.value < 0 || command.value > 65535) return false; break;
    case Action::Scan: case Action::Pause: case Action::Stop: break;
    default: return false;
    }
    if (xSemaphoreTake(impl_->snapshots, 0) != pdTRUE) return false;
    const uint32_t generation = impl_->published.generation;
    const bool exists = command.action != Action::Play ||
        (impl_->catalog && std::strcmp(impl_->published.state, "scanning") != 0 &&
         command.value <= impl_->published.tracks);
    xSemaphoreGive(impl_->snapshots);
    if (!exists) return false;
    const Impl::Queued request{command, generation};
    return xQueueSend(impl_->commands, &request, 0) == pdTRUE;
}

bool AudioService::status(Status& out)
{
    if (!ready() || xSemaphoreTake(impl_->snapshots, 0) != pdTRUE) return false;
    out = impl_->published;
    xSemaphoreGive(impl_->snapshots);
    return true;
}

bool AudioService::tracks(uint16_t playlist, size_t offset, size_t limit, Page& out)
{
    if (!ready() || limit < 1 || limit > MaxPage || xSemaphoreTake(impl_->snapshots, 0) != pdTRUE) return false;
    const bool result = impl_->catalog && std::strcmp(impl_->published.state, "scanning") != 0
        ? impl_->catalog->tracks(playlist, offset, limit, out) : false;
    xSemaphoreGive(impl_->snapshots);
    return result;
}

bool AudioService::playlists(size_t offset, size_t limit, Page& out)
{
    if (!ready() || limit < 1 || limit > MaxPage || xSemaphoreTake(impl_->snapshots, 0) != pdTRUE) return false;
    const bool result = impl_->catalog && std::strcmp(impl_->published.state, "scanning") != 0
        ? impl_->catalog->playlists(offset, limit, out) : false;
    xSemaphoreGive(impl_->snapshots);
    return result;
}

bool AudioService::copyCover(uint32_t& revision, bool& available, uint16_t* pixels, size_t count)
{
    if (!ready() || !pixels || count < CoverPixels || xSemaphoreTake(impl_->snapshots, 0) != pdTRUE) return false;
    available = impl_->coverAvailable;
    if (revision != impl_->coverRevision) {
        if (available) std::memcpy(pixels, impl_->coverPublished, CoverPixels * sizeof(uint16_t));
        revision = impl_->coverRevision;
    }
    xSemaphoreGive(impl_->snapshots);
    return true;
}
} // namespace meow::media
