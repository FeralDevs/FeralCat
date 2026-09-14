#pragma once
#include <cstddef>
#include <cstdint>

namespace audio_local {
struct SyncResult { bool found = false; std::size_t offset = 0; };

// Bound resynchronization after a seek. Read supplies up to 'count' bytes;
// cancellation is checked between chunks, so malformed tails cannot hold
// the worker indefinitely or reinterpret read() == -1 as a 0xff sync byte.
template<class Read, class Cancel>
SyncResult findMp3Sync(Read read, Cancel cancel, std::size_t maximum)
{
    unsigned char buffer[256];
    std::uint32_t header = 0;
    std::size_t consumed = 0;
    while (consumed < maximum && !cancel()) {
        const auto remaining = maximum - consumed;
        const auto wanted = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        const auto count = read(buffer, wanted);
        if (!count || count > wanted) break;
        for (std::size_t i = 0; i < count; ++i) {
            header = (header << 8) | buffer[i];
            ++consumed;
            if (consumed >= 4 && (header & 0xffe00000u) == 0xffe00000u &&
                ((header >> 19) & 3u) != 1u && // reserved MPEG version
                ((header >> 17) & 3u) == 1u && // Layer III
                ((header >> 12) & 15u) != 15u && // reserved bitrate
                ((header >> 10) & 3u) != 3u && // reserved sample rate
                (header & 3u) != 2u) { // reserved emphasis
                return {true, consumed - 4};
            }
        }
    }
    return {};
}
}
