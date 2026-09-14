#include "audio_dsp.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <new>

namespace {
unsigned checks = 0;
void require(bool condition, const char* message)
{
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
struct Coefficients { float a0, a1, a2, b1, b2; };
const Coefficients neutral{1, 0, 0, 0, 0};
const Coefficients feedback{0.5f, 0, 0, -0.5f, 0};

void requireZero(const audio_dsp::History& history)
{
    for (const auto& section : history.samples)
        for (const auto& delay : section)
            for (const auto& direction : delay)
                for (const float sample : direction)
                    require(sample == 0.0f, "all DSP feedback starts at zero");
}

void pipeline(audio_dsp::History& history, int16_t pair[2])
{
    for (auto& section : history.samples) audio_dsp::process(neutral, section, pair, pair);
}
}

int main()
{
    // Model Audio's dynamically allocated member: no value-initialized outer
    // object is allowed to accidentally hide the original missing initializer.
    alignas(audio_dsp::History) unsigned char storage[sizeof(audio_dsp::History)];
    std::memset(storage, 0xff, sizeof(storage));
    auto* history = new (storage) audio_dsp::History;
    requireZero(*history);
    for (const int value : {0, 1, -1, 12345, -23456, 32767, -32768}) {
        int16_t pair[2]{static_cast<int16_t>(value), static_cast<int16_t>(-value / 2)};
        const int16_t original[2]{pair[0], pair[1]};
        pipeline(*history, pair);
        require(pair[0] == original[0] && pair[1] == original[1], "three neutral production filters preserve stereo PCM");
    }

    // Exercise actual feedback math and demonstrate that resetting a track
    // removes prior signal history, not just a scratch output buffer.
    history->reset();
    int16_t impulse[2]{1000, -1000}, output[2]{};
    audio_dsp::process(feedback, history->samples[0], impulse, output);
    require(output[0] == 500 && output[1] == -500, "feedback filter processes both impulse channels");
    int16_t silence[2]{};
    audio_dsp::process(feedback, history->samples[0], silence, output);
    require(output[0] == 250 && output[1] == -250, "history affects subsequent samples within one track");
    history->reset();
    audio_dsp::process(feedback, history->samples[0], silence, output);
    require(output[0] == 0 && output[1] == 0, "new track does not inherit the previous track's ringing");

    // NaNs are a possible heap residue and persist in recursive filters even
    // when multiplied by zero. Never feed them into a float-to-int conversion;
    // the production reset must remove them before the first sample.
    for (auto& section : history->samples)
        for (auto& delay : section)
            for (auto& direction : delay)
                for (auto& sample : direction) sample = std::numeric_limits<float>::quiet_NaN();
    history->reset();
    requireZero(*history);
    int16_t music[2]{12000, -6000};
    pipeline(*history, music);
    require(music[0] == 12000 && music[1] == -6000, "poisoned feedback is removed before PCM processing");
    unsigned filterCalls = 0;
    auto filter = [&](int16_t* pcm) { ++filterCalls; pipeline(*history, pcm); return pcm; };
    bool exact = true;
    for (int value = -32768; value <= 32767; ++value) {
        int16_t stereo[2]{static_cast<int16_t>(value), static_cast<int16_t>(-value - 1)};
        const auto* out = audio_dsp::applyTone(0, 0, 0, stereo, filter);
        if (out != stereo || out[0] != value || out[1] != -value - 1) exact = false;
        int16_t mono[2]{static_cast<int16_t>(value), static_cast<int16_t>(value)};
        out = audio_dsp::applyTone(0, 0, 0, mono, filter);
        if (out != mono || out[0] != value || out[1] != value) exact = false;
    }
    require(exact && filterCalls == 0, "neutral production bypass preserves every 16-bit stereo/mono value without calling IIR");
    audio_dsp::applyTone(-1, 0, 0, music, filter);
    audio_dsp::applyTone(0, -1, 0, music, filter);
    audio_dsp::applyTone(0, 0, -1, music, filter);
    require(filterCalls == 3, "nonneutral tone still invokes the existing IIR chain");
    history->~History();
    std::printf("Audio DSP: %u checks passed.\n", checks);
    return 0;
}
