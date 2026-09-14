#pragma once
#include <cstddef>
#include <cstdint>

namespace audio_dsp {
// Keep one already processed stereo frame across nonblocking/partial DMA
// writes. Retrying a full DMA buffer must not run recursive DSP a second time.
class PendingFrame {
public:
    void reset() { word_ = 0; offset_ = sizeof(word_); }
    bool pending() const { return offset_ < sizeof(word_); }

    template<class Prepare, class Write>
    bool submit(Prepare prepare, Write write)
    {
        if (!pending()) {
            if (!prepare(word_)) return true; // Callback deliberately consumed it.
            offset_ = 0;
        }
        const size_t remaining = sizeof(word_) - offset_;
        const size_t written = write(reinterpret_cast<const uint8_t*>(&word_) + offset_, remaining);
        // Do not advance on an impossible driver byte count.
        if (written > remaining) return false;
        offset_ += written;
        return !pending();
    }
private:
    uint32_t word_ = 0;
    size_t offset_ = sizeof(word_);
};

inline uint32_t packStereo(int16_t left, int16_t right)
{
    // Shifting a negative signed sample (the previous Gain implementation)
    // is undefined C++; assemble the exact I2S bits with unsigned integers.
    return (static_cast<uint32_t>(static_cast<uint16_t>(left)) << 16) |
        static_cast<uint16_t>(right);
}
} // namespace audio_dsp
