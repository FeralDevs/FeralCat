#include "mp3_decoder.h"
#include "mp3_resync.h"
#include "audio_dsp.h"
#undef free
#include <cstdio>
#include <fstream>
#include <vector>
#include <limits>

namespace helixhost {
std::size_t calls = 0, live = 0, failAfter = std::numeric_limits<std::size_t>::max();
void* allocate(std::size_t size)
{
    if (calls++ >= failAfter) return nullptr;
    void* block = std::malloc(size);
    if (block) ++live;
    return block;
}
void release(void* block)
{
    if (block) { --live; std::free(block); }
}
}

namespace {
unsigned checks = 0;
void require(bool condition, const char* message)
{
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

void resyncTests()
{
    const unsigned char valid[] = {0xff, 0xfb, 0x90, 0x64};
    auto search = [](const std::vector<unsigned char>& bytes, std::size_t maximum,
                     unsigned cancelAfter, std::size_t& consumed, unsigned& reads) {
        consumed = 0;
        reads = 0;
        return audio_local::findMp3Sync(
            [&](unsigned char* out, std::size_t wanted) {
                ++reads;
                const auto left = bytes.size() - consumed;
                const auto count = wanted < left ? wanted : left;
                if (count) std::memcpy(out, bytes.data() + consumed, count);
                consumed += count;
                return count;
            }, [&]() { return reads >= cancelAfter; }, maximum);
    };
    std::size_t consumed;
    unsigned reads;
    std::vector<unsigned char> crossing(255, 0);
    crossing.insert(crossing.end(), valid, valid + sizeof(valid));
    auto result = search(crossing, 65536, UINT_MAX, consumed, reads);
    require(result.found && result.offset == 255, "MP3 sync found across read-chunk boundary");
    require(reads == 2 && consumed == crossing.size(), "chunked search reads only available bytes");
    for (const auto& malformed : std::vector<std::vector<unsigned char>>{
             {}, {0xff}, {0xff, 0xfb, 0x90}, {0xff, 0xeb, 0x90, 0x64},
             {0xff, 0xff, 0x90, 0x64}, {0xff, 0xfb, 0xfc, 0x64},
             {0xff, 0xfb, 0x9c, 0x64}, {0xff, 0xfb, 0x90, 0x66}}) {
        result = search(malformed, 65536, UINT_MAX, consumed, reads);
        require(!result.found, "EOF, incomplete or reserved MP3 header rejected");
        require(consumed == malformed.size() && reads <= 2, "corrupt short input stops at EOF");
    }
    std::vector<unsigned char> garbage(70000, 0);
    result = search(garbage, 65536, UINT_MAX, consumed, reads);
    require(!result.found && consumed == 65536 && reads == 256, "seek search has a hard 64 KiB work bound");
    result = search(crossing, 65536, 0, consumed, reads);
    require(!result.found && consumed == 0 && reads == 0, "cancellation before seek performs no reads");
    result = search(crossing, 65536, 1, consumed, reads);
    require(!result.found && consumed == 256 && reads == 1, "cancellation between chunks stops before next read");
    result = search(crossing, 255, UINT_MAX, consumed, reads);
    require(!result.found && consumed == 255, "frame after the byte budget cannot be returned");
    result = search(crossing, 0, UINT_MAX, consumed, reads);
    require(!result.found && reads == 0, "empty seek region performs no reads");
    for (const unsigned char versionByte : {static_cast<unsigned char>(0xf3), static_cast<unsigned char>(0xe3)}) {
        result = search({0xff, versionByte, 0x90, 0x64}, 65536, UINT_MAX, consumed, reads);
        require(result.found && result.offset == 0, "MPEG2 and MPEG2.5 frame sync supported");
    }
}

void decode(const std::vector<unsigned char>& data)
{
    require(MP3Decoder_AllocateBuffers(), "decoder allocation");
    // Guard samples ensure the real codec does not overrun its documented
    // maximum 1152 stereo samples (2304 interleaved values).
    short samples[2306]{};
    samples[0] = 12345;
    samples[2305] = -12345;
    std::size_t offset = 0;
    std::size_t sampleCount = 0;
    unsigned frames = 0;
    int peak = 0;
    std::uint64_t energy = 0;
    audio_dsp::History filterHistory;
    std::memset(&filterHistory, 0xff, sizeof(filterHistory));
    filterHistory.reset(); // Same reset used by Audio::setDefaults for a new track.
    struct Coefficients { float a0, a1, a2, b1, b2; };
    const Coefficients neutral{1, 0, 0, 0, 0};
    bool filteredPcmMatches = true;
    // ID3 headers are outside the raw Helix frame API; scan to real frame sync.
    auto input = data;
    while (offset + 4 < input.size()) {
        const int remaining = static_cast<int>(input.size() - offset);
        const int sync = MP3FindSyncWord(input.data() + offset, remaining);
        if (sync < 0) break;
        offset += static_cast<std::size_t>(sync);
        int bytesLeft = static_cast<int>(input.size() - offset);
        const int before = bytesLeft;
        const int result = MP3Decode(input.data() + offset, &bytesLeft, samples + 1, 0);
        require(samples[0] == 12345 && samples[2305] == -12345, "PCM output guards intact");
        require(bytesLeft >= 0 && bytesLeft <= before, "decoder consumed a bounded byte count");
        const auto consumed = static_cast<std::size_t>(before - bytesLeft);
        if (result == ERR_MP3_INDATA_UNDERFLOW) break;
        require(result == ERR_MP3_NONE || result == ERR_MP3_MAINDATA_UNDERFLOW,
                "valid fixture frame decoded without corrupt-frame errors");
        require(consumed > 0, "frame decode makes progress");
        offset += consumed;
        if (result == ERR_MP3_NONE) {
            require(MP3GetSampRate() == 44100 && MP3GetChannels() == 2 && MP3GetBitsPerSample() == 16,
                    "fixture decodes to stereo 44.1 kHz / 16 bit");
            const int produced = MP3GetOutputSamps();
            require(produced > 0 && produced <= 2304 && produced % 2 == 0, "stereo PCM sample count is bounded and even");
            for (int i = 1; i <= produced; i += 2) {
                int16_t pair[2]{samples[i], samples[i + 1]};
                for (auto& section : filterHistory.samples) audio_dsp::process(neutral, section, pair, pair);
                if (pair[0] != samples[i] || pair[1] != samples[i + 1]) filteredPcmMatches = false;
                for (const int value : pair) {
                    const int magnitude = value < 0 ? -value : value;
                    if (magnitude > peak) peak = magnitude;
                    energy += static_cast<std::uint64_t>(value * value);
                }
            }
            sampleCount += static_cast<std::size_t>(produced);
            ++frames;
        }
        require(frames < 300, "bounded three-second fixture frame count");
    }
    require(frames > 100 && sampleCount >= 2 * 44100 * 3 && sampleCount < 2 * 44100 * 4,
            "three seconds of real MP3 decoded including frame padding");
    require(peak > 0 && energy > 0, "decoded PCM is audible data rather than all zeros");
    require(filteredPcmMatches, "real MP3 PCM survives three production filters after poisoned-history reset");
    MP3Decoder_FreeBuffers();
    require(helixhost::live == 0, "all decoder allocations released");
    std::printf("Decoded %u frames, %zu interleaved samples, peak %d.\n", frames, sampleCount, peak);
}
}

int main(int argc, char** argv)
{
    require(argc == 2, "usage: mp3_decode_test <three-second-stereo-44100.mp3>");
    std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
    require(input.good(), "real MP3 fixture exists");
    const auto length = input.tellg();
    require(length > 0 && length < 1024 * 1024, "fixture size bounded");
    std::vector<unsigned char> data(static_cast<std::size_t>(length));
    input.seekg(0);
    require(static_cast<bool>(input.read(reinterpret_cast<char*>(data.data()), length)), "complete fixture read");
    resyncTests();
    for (int repeat = 0; repeat < 3; ++repeat) decode(data);
    MP3Decoder_FreeBuffers();
    require(helixhost::live == 0, "repeated decoder cleanup is idempotent");
    for (std::size_t failure = 0; failure < 9; ++failure) {
        helixhost::calls = 0;
        helixhost::failAfter = failure;
        require(!MP3Decoder_AllocateBuffers(), "injected decoder allocation failure reported");
        require(helixhost::live == 0, "partial decoder allocation failure releases every block");
        MP3Decoder_FreeBuffers();
    }
    helixhost::failAfter = std::numeric_limits<std::size_t>::max();
    require(MP3Decoder_AllocateBuffers(), "decoder usable after allocation failures");
    unsigned char invalid[64]{};
    require(MP3FindSyncWord(invalid, sizeof(invalid)) < 0, "non-MP3 data has no frame sync");
    short output[2304]{};
    int invalidBytes = sizeof(invalid);
    require(MP3Decode(invalid, &invalidBytes, output, 0) < 0, "non-MP3 frame rejected");
    MP3Decoder_FreeBuffers();
    require(helixhost::live == 0, "invalid frame cleanup");
    std::printf("Helix MP3 decode: %u checks passed (host codec, no I2S/device claim).\n", checks);
    return 0;
}
