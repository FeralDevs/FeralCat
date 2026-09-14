#pragma once
#include "media_api.h"
#include "media_paths.h"
#include <FS.h>

namespace meow::media {
constexpr size_t MaxTracks = 512, MaxPlaylists = 16, MaxMediaPath = 191;
constexpr size_t MaxScanEntries = 4096, MaxPlaylistBytes = 65536, MaxPlaylistLine = 255;
constexpr unsigned MaxScanDepth = 4;

// Scan only an unpublished instance on the audio worker. The service owns the
// publication mutex and generation. All const getters copy memory and do no I/O.
class MediaCatalog {
public:
    enum class ScanResult { Complete, Cancelled, Error };
    using Cancel = bool (*)(void*);
    MediaCatalog() = default;
    ~MediaCatalog();
    MediaCatalog(const MediaCatalog&) = delete;
    MediaCatalog& operator=(const MediaCatalog&) = delete;
    ScanResult scan(fs::FS& source, Cancel cancel = nullptr, void* context = nullptr);
    bool tracks(uint16_t playlist, size_t offset, size_t limit, Page& out) const;
    bool playlists(size_t offset, size_t limit, Page& out) const;
    bool track(uint16_t id, char* path, size_t pathCapacity,
               char* title, size_t titleCapacity) const;
    uint16_t trackCount() const { return trackCount_; }
    uint16_t playlistCount() const { return playlistCount_; }
    uint16_t skipped() const { return skipped_; }
    const char* error() const { return error_; }
private:
    struct Track { char path[MaxMediaPath + 1]; char title[96]; };
    struct Playlist { char path[MaxMediaPath + 1]; char title[96]; uint16_t count; };
    Track* records_ = nullptr;
    Playlist* lists_ = nullptr;
    uint16_t* indices_ = nullptr;
    uint16_t trackCount_ = 0, playlistCount_ = 0, skipped_ = 0;
    size_t inspected_ = 0;
    char error_[160]{};
    Cancel cancel_ = nullptr;
    void* cancelContext_ = nullptr;
    bool cancelled_ = false, exhausted_ = false;
    void clear();
    void skip();
    bool isCancelled();
    bool walk(fs::FS& source, const char* directory, unsigned depth);
    bool readPlaylist(fs::FS& source, size_t index);
    void playlistLine(size_t index, const char* line, size_t length, bool first);
    uint16_t findTrack(const char* path) const;
};
} // namespace meow::media
