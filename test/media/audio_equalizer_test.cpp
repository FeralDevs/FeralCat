#include "audio_equalizer.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <initializer_list>

using meow::media::Equalizer;
namespace {
unsigned checks = 0;
void require(bool value, const char* label)
{
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}
double response(unsigned preset, unsigned rate, double frequency)
{
    Equalizer eq;
    eq.setSampleRate(rate);
    eq.setPreset(preset);
    eq.reset();
    double inputEnergy = 0, outputEnergy = 0;
    for (unsigned i = 0; i < rate / 2; ++i) {
        const int16_t source = static_cast<int16_t>(20000 * std::sin(6.283185307179586 * frequency * i / rate));
        int16_t pair[2]{source, 0};
        eq.process(pair);
        if (i > rate / 4) {
            inputEnergy += double(source) * source;
            outputEnergy += double(pair[0]) * pair[0];
        }
        if (pair[1] != 0) require(false, "channel isolation");
    }
    return std::sqrt(outputEnergy / inputEnergy);
}
}
int main()
{
    Equalizer eq;
    require(!eq.setPreset(-1) && !eq.setPreset(4) && eq.preset() == 0, "invalid presets rejected without state changes");
    require(!eq.setSampleRate(0) && !eq.setSampleRate(96000), "unsupported rates rejected");
    for (int value = -32768; value <= 32767; ++value) {
        int16_t pair[2]{static_cast<int16_t>(value), static_cast<int16_t>(-value - 1)};
        eq.process(pair);
        if (pair[0] != value || pair[1] != -value - 1) require(false, "neutral is bit exact over complete PCM range");
    }
    require(true, "neutral preserves all 65536 signed PCM values");
    uint32_t packed = 0x80007fff;
    eq.processWord(packed);
    require(packed == 0x80007fff, "packed neutral samples preserve I2S slot bits");
    uint32_t random = 0xdec0de;
    for (const unsigned rate : {22050u, 32000u, 44100u, 48000u}) {
        for (unsigned preset = 0; preset < 4; ++preset) {
            require(eq.setSampleRate(rate) && eq.setPreset(preset), "supported preset/rate accepted");
            eq.reset();
            bool bounded = true;
            for (unsigned i = 0; i < 200000; ++i) {
                random ^= random << 13; random ^= random >> 17; random ^= random << 5;
                // Alternating full scale, impulses and smaller random signals.
                const int amplitude = i % 3 == 0 ? 32767 : 12000;
                const int source = i % 5 == 0 ? amplitude : i % 5 == 1 ? -amplitude :
                    static_cast<int>(random % (amplitude * 2 + 1)) - amplitude;
                int16_t pair[2]{static_cast<int16_t>(source), 0};
                eq.process(pair);
                if (pair[1] != 0) bounded = false;
            }
            require(bounded, "long PCM sequence preserves channel isolation and terminates");
            eq.reset();
            bool amplitudeBounded = true;
            for (unsigned i = 0; i < 30000; ++i) {
                int16_t pair[2]{static_cast<int16_t>(i & 1 ? 12000 : -12000), 0};
                eq.process(pair);
                if (pair[0] < -12001 || pair[0] > 12001 || pair[1]) amplitudeBounded = false;
            }
            require(amplitudeBounded, "transient output never exceeds bounded source amplitude");
            eq.reset();
            int16_t silence[2]{};
            eq.process(silence);
            require(silence[0] == 0 && silence[1] == 0, "track reset removes all filter residue");
            for (const double frequency : {50.0, 250.0, 1000.0, 3500.0, 9000.0})
                require(response(preset, rate, frequency) <= 1.0001, "preset frequency response never boosts signal");
        }
        require(response(1, rate, 50) < response(1, rate, 1000), "voice reduces low bass relative to speech");
        require(response(2, rate, 9000) < response(2, rate, 50), "warm reduces treble relative to bass");
        require(response(3, rate, 50) < response(3, rate, 1000), "small speaker reduces low bass");
    }
    eq.setSampleRate(44100);
    eq.setPreset(0); eq.reset();
    int16_t previous = 16000;
    bool smooth = true;
    for (unsigned i = 0; i < 1024; ++i) { int16_t pair[2]{16000, 0}; eq.process(pair); }
    eq.setPreset(3);
    for (unsigned i = 0; i < 512; ++i) {
        int16_t pair[2]{16000, 0}; eq.process(pair);
        if (std::abs(int(pair[0]) - previous) > 128) smooth = false;
        previous = pair[0];
    }
    require(smooth, "preset crossfade avoids a discontinuous DC level jump");
    eq.setPreset(0);
    for (unsigned i = 0; i < 256; ++i) { int16_t pair[2]{12345, -12345}; eq.process(pair); }
    int16_t pair[2]{32767, -32768}; eq.process(pair);
    require(pair[0] == 32767 && pair[1] == -32768, "neutral becomes bit exact after bounded transition");
    std::printf("Audio equalizer: %u checks passed; 3.2 million stress frames, four sample rates.\n", checks);
}
