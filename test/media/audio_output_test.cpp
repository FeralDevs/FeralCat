#include "audio_output.h"
#include "audio_dsp.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
unsigned checks = 0;
void require(bool value, const char* label)
{
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}
struct Coefficients { float a0, a1, a2, b1, b2; };
}

int main()
{
    using audio_dsp::PendingFrame;
    require(audio_dsp::packStereo(-32768, 32767) == 0x80007fff, "signed extrema pack into exact I2S bits");
    require(audio_dsp::packStereo(-1, -2) == 0xfffffffe, "negative sample packing has no signed shifts");
    PendingFrame pending;
    audio_dsp::History history;
    const Coefficients feedback{0.5f, 0, 0, -0.5f, 0};
    unsigned prepared = 0;
    auto prepare = [&](uint32_t& word) {
        ++prepared;
        int16_t input[2]{1000, -1000}, output[2]{};
        audio_dsp::process(feedback, history.samples[0], input, output);
        word = audio_dsp::packStereo(output[0], output[1]);
        return true;
    };
    auto full = [](const uint8_t*, size_t) { return size_t(0); };
    require(!pending.submit(prepare, full), "full DMA buffer leaves frame pending");
    for (unsigned i = 0; i < 100; ++i) require(!pending.submit(prepare, full), "retry stays pending without DMA progress");
    require(prepared == 1, "recursive production DSP runs once across one hundred retries");
    std::vector<uint8_t> bytes;
    auto partial = [&](const uint8_t* source, size_t count) {
        const size_t accepted = count > 1 ? 1 : count;
        bytes.insert(bytes.end(), source, source + accepted);
        return accepted;
    };
    for (unsigned i = 0; i < 4; ++i)
        require(pending.submit(prepare, partial) == (i == 3), "partial DMA writes complete exactly one frame");
    uint32_t output = 0;
    std::memcpy(&output, bytes.data(), sizeof(output));
    require(bytes.size() == 4 && output == audio_dsp::packStereo(500, -500), "partial writes preserve all bytes in order");
    require(prepared == 1, "partial writes do not advance filter history again");
    auto complete = [&](const uint8_t* source, size_t count) {
        std::memcpy(&output, source, count);
        return count;
    };
    require(pending.submit(prepare, complete) && prepared == 2, "next decoded frame advances DSP once");
    require(output == audio_dsp::packStereo(750, -750), "feedback after retries matches uninterrupted filtering");
    require(!pending.submit(prepare, full), "new pending sample created");
    pending.reset(); // Used by Audio stopSong, pause and setFilePos.
    require(!pending.pending(), "stop/pause/seek discards retained frame");
    unsigned writes = 0;
    require(pending.submit([](uint32_t&) { return false; }, [&](const uint8_t*, size_t n) { ++writes; return n; }),
            "callback can consume a sample without sending it");
    require(writes == 0 && !pending.pending(), "cancelled sample never enters DMA");
    require(!pending.submit(prepare, [](const uint8_t*, size_t n) { return n + 1; }), "impossible byte count rejected");
    const unsigned before = prepared;
    require(pending.submit(prepare, complete) && prepared == before, "invalid count cannot double-process PCM");
    pending.reset();

    // Sustained exact byte-order test: zero progress and arbitrary partial
    // writes, without a second DSP pass or a dropped/duplicated frame.
    uint32_t sequence = 0;
    size_t calls = 0, consumed = 0;
    std::vector<uint8_t> stream;
    auto generate = [&](uint32_t& word) { word = sequence++; return true; };
    auto transport = [&](const uint8_t* source, size_t count) {
        const size_t budget = (++calls * 17) % 5;
        const size_t accepted = count < budget ? count : budget;
        stream.insert(stream.end(), source, source + accepted);
        return accepted;
    };
    while (consumed < 100000) if (pending.submit(generate, transport)) ++consumed;
    require(sequence == consumed && stream.size() == consumed * 4, "one hundred thousand frames survive transport backpressure");
    bool ordered = true;
    for (size_t i = 0; i < consumed; ++i) {
        uint32_t word;
        std::memcpy(&word, stream.data() + i * 4, 4);
        if (word != i) ordered = false;
    }
    require(ordered, "sustained DMA byte stream has no duplicates or gaps");
    std::printf("Audio output: %u checks passed; 100000 backpressured frames.\n", checks);
}
