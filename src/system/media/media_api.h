#pragma once
#include <cstddef>
#include <cstdint>

namespace meow::media {
constexpr size_t MaxPage = 8;
struct Entry {
    uint16_t id = 0; // One-based stable ID for this catalog generation.
    char title[96]{};
    uint16_t count = 0; // Playlist size, zero for a track.
};
struct Page {
    Entry entries[MaxPage]{};
    size_t count = 0;
    size_t total = 0;
};
struct Status {
    char state[16] = "idle"; // idle/scanning/loading/playing/paused/stopped/ended/error
    char title[96]{};
    char error[160]{};
    uint32_t position = 0, duration = 0; // seconds; MP3 duration can be approximate
    uint32_t generation = 0; // changes when a catalog scan completes
    uint16_t track = 0, tracks = 0, playlists = 0;
    uint8_t volume = 35; // Digital 0..100, codec output remains capped.
    uint8_t equalizer = 0; // 0 neutral, 1 voice, 2 warm, 3 small speaker; attenuation only.
    uint32_t finished = 0; // Increments once on natural EOF, never on explicit stop.
    uint32_t playback = 0; // Increments on each executed Play, even for the same track ID.
    uint16_t skipped = 0; // Invalid/unindexed paths or playlist entries.
};
enum class Action { Scan, Play, Pause, Stop, Volume, Seek, Equalizer };
struct Command { Action action; int32_t value = 0; };
// All methods called from Lua must return promptly. Audio, SD access and
// decoding belong to a worker; getters copy bounded immutable snapshots.
class Host {
public:
    virtual ~Host() = default;
    virtual bool command(Command command) = 0;
    virtual bool status(Status& out) = 0;
    // playlist=0 is all tracks; playlist>0 selects a scanned M3U playlist.
    // offset is zero based; limit is 1..8. Returns indexed track IDs.
    virtual bool tracks(uint16_t playlist, size_t offset, size_t limit, Page& out) = 0;
    virtual bool playlists(size_t offset, size_t limit, Page& out) = 0;
};
} // namespace meow::media
