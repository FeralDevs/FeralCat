#pragma once
#include <cmath>
#include <cstdint>

namespace meow::media {
enum class EqualizerPreset : uint8_t { Neutral, Voice, Warm, SmallSpeaker };

// Two one-pole low-pass states divide low/middle/high content. Presets only
// attenuate bands. Normalize the sum of absolute mixing coefficients to <=1:
// each state is a convex average of bounded PCM, so no input or transient can
// gain amplitude beyond the PCM range. No automatic loudness compensation.
class Equalizer {
public:
    Equalizer() { setSampleRate(44100); }
    static bool validPreset(int value) { return value >= 0 && value <= 3; }
    bool setSampleRate(uint32_t rate)
    {
        if (rate != 22050 && rate != 32000 && rate != 44100 && rate != 48000) return false;
        if (rate_ == rate) return true;
        rate_ = rate;
        lowAlpha_ = 1.0f - std::exp(-6.28318530718f * 250.0f / rate);
        highAlpha_ = 1.0f - std::exp(-6.28318530718f * 3500.0f / rate);
        reset();
        return true;
    }
    bool setPreset(int value)
    {
        if (!validPreset(value)) return false;
        if (preset_ == value) return true;
        preset_ = static_cast<uint8_t>(value);
        float low = 1, middle = 1, high = 1;
        switch (static_cast<EqualizerPreset>(value)) {
        case EqualizerPreset::Voice: low = 0.35f; high = 0.75f; break;
        case EqualizerPreset::Warm: middle = 0.85f; high = 0.55f; break;
        case EqualizerPreset::SmallSpeaker: low = 0.20f; high = 0.80f; break;
        case EqualizerPreset::Neutral: break;
        }
        target_[0] = low - middle;
        target_[1] = middle - high;
        target_[2] = high;
        const float norm = std::fabs(target_[0]) + std::fabs(target_[1]) + std::fabs(target_[2]);
        if (norm > 1) for (float& weight : target_) weight /= norm;
        // Crossfade the weights instead of discontinuously clearing live
        // filters; a convex mix of two safe coefficient sets remains safe.
        remaining_ = RampFrames;
        for (unsigned i = 0; i < 3; ++i) start_[i] = weights_[i];
        return true;
    }
    void reset()
    {
        for (auto& channel : history_) for (float& state : channel) state = 0;
        for (unsigned i = 0; i < 3; ++i) weights_[i] = target_[i];
        remaining_ = 0;
    }
    uint8_t preset() const { return preset_; }
    void process(int16_t samples[2])
    {
        if (remaining_) {
            --remaining_;
            const float blend = static_cast<float>(remaining_) / RampFrames;
            for (unsigned i = 0; i < 3; ++i)
                weights_[i] = remaining_ ? start_[i] * blend + target_[i] * (1.0f - blend) : target_[i];
        }
        for (unsigned channel = 0; channel < 2; ++channel) {
            const float current = samples[channel];
            history_[channel][0] += lowAlpha_ * (current - history_[channel][0]);
            history_[channel][1] += highAlpha_ * (current - history_[channel][1]);
            if (preset_ == 0 && !remaining_) continue; // Bit-exact neutral path.
            float result = weights_[0] * history_[channel][0] +
                weights_[1] * history_[channel][1] + weights_[2] * current;
            // Only protects against floating-point roundoff at the endpoints;
            // the coefficient norm already bounds the actual signal gain.
            if (result > 32767.0f) result = 32767.0f;
            if (result < -32768.0f) result = -32768.0f;
            samples[channel] = static_cast<int16_t>(result);
        }
    }
    void processWord(uint32_t& word)
    {
        const uint32_t left = word >> 16, right = word & 0xffff;
        int16_t samples[2]{static_cast<int16_t>(static_cast<int32_t>(left) - (left >= 32768 ? 65536 : 0)),
                           static_cast<int16_t>(static_cast<int32_t>(right) - (right >= 32768 ? 65536 : 0))};
        process(samples);
        word = (static_cast<uint32_t>(static_cast<uint16_t>(samples[0])) << 16) |
            static_cast<uint16_t>(samples[1]);
    }
private:
    static constexpr unsigned RampFrames = 256;
    uint32_t rate_ = 0;
    uint8_t preset_ = 0;
    unsigned remaining_ = 0;
    float lowAlpha_ = 0, highAlpha_ = 0;
    float history_[2][2]{};
    float weights_[3]{0, 0, 1}, target_[3]{0, 0, 1}, start_[3]{0, 0, 1};
};
} // namespace meow::media
