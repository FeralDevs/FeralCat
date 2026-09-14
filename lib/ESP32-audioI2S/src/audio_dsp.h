#pragma once
#include <cstdint>
#include <cstring>

namespace audio_dsp {
// Three stereo biquads, indexed [section][delay][input/output][channel].
// A dynamically allocated Audio object must never inherit heap contents here.
struct History {
    float samples[3][2][2][2]{};
    void reset() { std::memset(samples, 0, sizeof(samples)); }
};

template<class Filter>
int16_t* applyTone(int8_t low, int8_t middle, int8_t high, int16_t samples[2], Filter filter)
{
    // A neutral tone is exact integer passthrough, without recursive float
    // cancellation/roundoff or unnecessary per-sample biquad work.
    return (low == 0 && middle == 0 && high == 0) ? samples : filter(samples);
}

// Shared production kernel for the three original IIR_filterChain methods.
// Keep the original arithmetic/order and 16-bit output conversion unchanged.
template<class Coefficients>
void process(const Coefficients& coefficients, float (&history)[2][2][2],
             const int16_t input[2], int16_t output[2])
{
    for (unsigned channel = 0; channel < 2; ++channel) {
        const float current = static_cast<float>(input[channel]);
        const float filtered = coefficients.a0 * current +
            coefficients.a1 * history[0][0][channel] +
            coefficients.a2 * history[1][0][channel] -
            coefficients.b1 * history[0][1][channel] -
            coefficients.b2 * history[1][1][channel];
        history[1][0][channel] = history[0][0][channel];
        history[0][0][channel] = current;
        history[1][1][channel] = history[0][1][channel];
        history[0][1][channel] = filtered;
        output[channel] = static_cast<int16_t>(filtered);
    }
}
} // namespace audio_dsp
