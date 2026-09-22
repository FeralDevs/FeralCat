#pragma once

#include <cstddef>
#include <cstdint>

class DEVICES;

namespace meow::nes {

struct AvStats {
    uint32_t frames = 0;
    uint32_t dmaWaits = 0;
    uint32_t lastFrameUs = 0;
    uint32_t maxFrameUs = 0;
    uint32_t audioSamplesQueued = 0;
    uint32_t audioSamplesWritten = 0;
    uint32_t audioQueueTimeouts = 0;
    uint32_t audioWriteTimeouts = 0;
    uint32_t audioErrors = 0;
    // A starvation incident after playback starts, not an I2S hardware counter.
    uint32_t audioUnderruns = 0;
    uint32_t stopTimeouts = 0;
};

// Pure scheduling policy: reserve enough software PCM for the measured LCD
// plus next core step, capped at two frames. A stale slow LCD measurement must
// not suppress every future draw: permit a retry after 250 ms, after prefill.
bool audioAllowsDisplay(bool prefilling, size_t pendingSamples, size_t frameSamples,
                        uint32_t coreUs, uint32_t drawUs, uint32_t sinceLastDrawMs);

// Single-owner session HAL. Call every public method from the session task.
// The session must stop all other LCD/audio users before begin(). In particular,
// AudioService, mic and speaker must be stopped; this class owns I2S0 until stop.
class Av final {
public:
    static constexpr int Width = 256;
    static constexpr int Height = 240;
    static constexpr int Left = 32;
    static constexpr uint32_t SampleRate = 44100;

    Av() = default;
    ~Av();
    Av(const Av&) = delete;
    Av& operator=(const Av&) = delete;

    bool begin(DEVICES* device, bool enableAudio = true);

    // Owner-only, clamped to 0..100; default 100. Persists across stop/begin.
    // Applied atomically when the worker converts the next PCM block. Samples
    // already converted or queued in I2S finish at their previous volume.
    void setVolume(unsigned percent);

    // indexed already points at the first visible pixel (e.g. pitch=272).
    // palette565 contains 256 native-endian RGB565 entries. Only x=32..287 is
    // written. On return all DMA has finished: the core may reuse both inputs,
    // and the owner may draw sidebars. No asynchronous frame ownership escapes.
    bool submitFrame(const uint8_t* indexed, size_t pitch,
                     const uint16_t* palette565);

    // Copies PCM into a bounded queue. Returns the accepted prefix length;
    // 0 means disabled, stopped, invalid input, or queue timeout. Never retains
    // caller memory. timeoutMs is a TOTAL call budget, capped at 100 ms, with
    // FreeRTOS tick/scheduling granularity (not a hard real-time guarantee).
    // Retain/retry any unaccepted suffix before advancing the emulator. Blocking
    // on this queue supplies I2S backpressure; no second frame timer is needed.
    size_t submitAudio(const int16_t* mono, size_t count, uint32_t timeoutMs = 100);

    // Accepted samples still in the software queue/worker, excluding I2S DMA.
    // A conservative reserve for deciding whether a synchronous LCD draw fits.
    size_t audioPendingSamples() const;
    // Produce PCM without a frame-timer sleep while the initial/recovery reserve
    // is being filled. false also when audio is disabled/stopped/failed.
    bool audioPrefilling() const;

    // Cancel queued audio, wait for worker acknowledgement, then release codec,
    // I2S, queues and DMA strips. false retains ownership/resources so the owner
    // can retry; never reuse the audio hardware after a failed stop.
    bool stop(uint32_t timeoutMs = 1000);
    bool active() const;
    AvStats stats() const;
    const char* error() const { return error_; }

private:
    struct Impl;
    Impl* impl_ = nullptr;
    AvStats lastStats_{};
    unsigned volume_ = 100;
    char error_[96]{};
    void setError(const char* text);
};

} // namespace meow::nes
