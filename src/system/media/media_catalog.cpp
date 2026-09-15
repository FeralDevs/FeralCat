#include "media_catalog.h"
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstring>
#include <cstdio>

namespace meow::media {
namespace {
size_t boundedLength(const char* text, size_t limit)
{
    size_t n = 0;
    if (!text) return limit + 1;
    while (n <= limit && text[n]) ++n;
    return n;
}
bool extension(const char* path, const char* suffix)
{
    const size_t n = std::strlen(path), s = std::strlen(suffix);
    return n >= s && compareMediaPaths(path + n - s, suffix) == 0;
}
void titleFromPath(const char* path, char* title)
{
    const char* leaf = std::strrchr(path, '/');
    leaf = leaf ? leaf + 1 : path;
    const char* dot = std::strrchr(leaf, '.');
    size_t length = dot && dot != leaf ? static_cast<size_t>(dot - leaf) : std::strlen(leaf);
    if (length > 95) {
        length = 95;
        while (length && (static_cast<unsigned char>(leaf[length]) & 0xc0) == 0x80) --length;
    }
    std::memcpy(title, leaf, length);
    title[length] = 0;
}
}

MediaCatalog::~MediaCatalog() { clear(); }
void MediaCatalog::clear()
{
    if (records_) heap_caps_free(records_);
    if (lists_) heap_caps_free(lists_);
    if (indices_) heap_caps_free(indices_);
    records_ = nullptr; lists_ = nullptr; indices_ = nullptr;
    trackCount_ = playlistCount_ = skipped_ = 0;
    inspected_ = 0; exhausted_ = cancelled_ = false; error_[0] = 0;
}
void MediaCatalog::skip() { if (skipped_ != UINT16_MAX) ++skipped_; }
bool MediaCatalog::isCancelled()
{
    if (cancel_ && cancel_(cancelContext_)) cancelled_ = true;
    return cancelled_;
}

MediaCatalog::ScanResult MediaCatalog::scan(fs::FS& source, Cancel cancel, void* context)
{
    clear(); cancel_ = cancel; cancelContext_ = context;
    if (isCancelled()) { clear(); return ScanResult::Cancelled; }
    constexpr unsigned caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    records_ = static_cast<Track*>(heap_caps_malloc(sizeof(Track) * MaxTracks, caps));
    lists_ = static_cast<Playlist*>(heap_caps_malloc(sizeof(Playlist) * MaxPlaylists, caps));
    indices_ = static_cast<uint16_t*>(heap_caps_malloc(sizeof(uint16_t) * MaxPlaylists * MaxTracks, caps));
    if (!records_ || !lists_ || !indices_) {
        clear(); std::snprintf(error_, sizeof(error_), "Not enough PSRAM for media catalog");
        return ScanResult::Error;
    }
    std::memset(records_, 0, sizeof(Track) * MaxTracks);
    std::memset(lists_, 0, sizeof(Playlist) * MaxPlaylists);
    std::memset(indices_, 0, sizeof(uint16_t) * MaxPlaylists * MaxTracks);
    if (!walk(source, "/music", 0)) {
        const bool cancelled = cancelled_;
        char reason[sizeof(error_)]; std::memcpy(reason, error_, sizeof(reason));
        clear(); std::memcpy(error_, reason, sizeof(error_));
        return cancelled ? ScanResult::Cancelled : ScanResult::Error;
    }
    std::sort(records_, records_ + trackCount_, [](const Track& a, const Track& b) {
        return compareMediaPaths(a.path, b.path) < 0;
    });
    std::sort(lists_, lists_ + playlistCount_, [](const Playlist& a, const Playlist& b) {
        return compareMediaPaths(a.path, b.path) < 0;
    });
    for (size_t i = 0; i < playlistCount_; ++i) {
        if (isCancelled() || !readPlaylist(source, i)) {
            clear(); return ScanResult::Cancelled;
        }
    }
    if (isCancelled()) { clear(); return ScanResult::Cancelled; }
    cancel_ = nullptr; cancelContext_ = nullptr;
    return ScanResult::Complete;
}

bool MediaCatalog::walk(fs::FS& source, const char* path, unsigned depth)
{
    if (isCancelled()) return false;
    File directory = source.open(path, FILE_READ);
    if (!directory || !directory.isDirectory()) {
        directory.close();
        if (!depth) { std::snprintf(error_, sizeof(error_), "Music folder /music is unavailable"); return false; }
        skip(); return true;
    }
    while (!exhausted_) {
        if (isCancelled()) { directory.close(); return false; }
        // The cap includes files and directories; do not open a 4097th entry.
        if (inspected_ >= MaxScanEntries) { exhausted_ = true; skip(); break; }
        File entry = directory.openNextFile();
        if (!entry) break;
        ++inspected_;
        const bool isDirectory = entry.isDirectory();
        const char* name = entry.name();
        const size_t length = boundedLength(name, MaxMediaPath);
        char child[MaxMediaPath + 1]{};
        bool safe = length && length <= MaxMediaPath && validMediaText(name, length);
        // File.name() is a leaf name in the pinned Arduino FS implementation.
        for (size_t i = 0; safe && i < length; ++i)
            if (name[i] == '/' || name[i] == '\\') safe = false;
        if (safe && (!std::strcmp(name, ".") || !std::strcmp(name, ".."))) safe = false;
        if (safe) safe = normalizeMediaPath(path, name, length, child, sizeof(child));
        entry.close();
        if (isCancelled()) { directory.close(); return false; }
        if (!safe) { skip(); continue; }
        if (isDirectory) {
            if (depth >= MaxScanDepth) { skip(); continue; }
            if (!walk(source, child, depth + 1)) { directory.close(); return false; }
        } else if (extension(child, ".mp3")) {
            bool duplicate = false;
            for (size_t i = 0; i < trackCount_; ++i)
                if (!compareMediaPaths(records_[i].path, child)) { duplicate = true; break; }
            if (duplicate) { skip(); continue; }
            size_t slot = trackCount_;
            if (trackCount_ == MaxTracks) {
                // Retain the first 512 sorted paths, independent of SD enumeration order.
                skip(); slot = 0;
                for (size_t i = 1; i < trackCount_; ++i)
                    if (compareMediaPaths(records_[i].path, records_[slot].path) > 0) slot = i;
                if (compareMediaPaths(child, records_[slot].path) >= 0) continue;
            } else ++trackCount_;
            Track& track = records_[slot];
            std::strcpy(track.path, child); titleFromPath(child, track.title);
        } else if (extension(child, ".m3u") || extension(child, ".m3u8")) {
            bool duplicate = false;
            for (size_t i = 0; i < playlistCount_; ++i)
                if (!compareMediaPaths(lists_[i].path, child)) { duplicate = true; break; }
            if (duplicate) { skip(); continue; }
            size_t slot = playlistCount_;
            if (playlistCount_ == MaxPlaylists) {
                skip(); slot = 0;
                for (size_t i = 1; i < playlistCount_; ++i)
                    if (compareMediaPaths(lists_[i].path, lists_[slot].path) > 0) slot = i;
                if (compareMediaPaths(child, lists_[slot].path) >= 0) continue;
            } else ++playlistCount_;
            Playlist& list = lists_[slot];
            std::strcpy(list.path, child); titleFromPath(child, list.title);
        }
    }
    directory.close();
    return !cancelled_;
}

uint16_t MediaCatalog::findTrack(const char* path) const
{
    size_t lo = 0, hi = trackCount_;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        const int order = compareMediaPaths(records_[mid].path, path);
        if (!order) return static_cast<uint16_t>(mid + 1);
        if (order < 0) lo = mid + 1; else hi = mid;
    }
    return 0;
}

void MediaCatalog::playlistLine(size_t index, const char* text, size_t length, bool first)
{
    if (first && length >= 3 && static_cast<unsigned char>(text[0]) == 0xef &&
        static_cast<unsigned char>(text[1]) == 0xbb && static_cast<unsigned char>(text[2]) == 0xbf) {
        text += 3; length -= 3;
    }
    if (length && text[length - 1] == '\r') --length;
    // Whitespace surrounding an entry is not part of the file name.
    while (length && (*text == ' ' || *text == '\t')) { ++text; --length; }
    while (length && (text[length - 1] == ' ' || text[length - 1] == '\t')) --length;
    if (!length) return;
    if (!validMediaText(text, length)) { skip(); return; }
    if (text[0] == '#') return;
    char folder[MaxMediaPath + 1], path[MaxMediaPath + 1];
    std::strcpy(folder, lists_[index].path);
    char* slash = std::strrchr(folder, '/');
    if (!slash) { skip(); return; }
    *slash = 0;
    if (!normalizeMediaPath(folder, text, length, path, sizeof(path)) || !extension(path, ".mp3")) {
        skip(); return;
    }
    const uint16_t id = findTrack(path);
    if (!id || lists_[index].count == MaxTracks) { skip(); return; }
    indices_[index * MaxTracks + lists_[index].count++] = id;
}

bool MediaCatalog::readPlaylist(fs::FS& source, size_t index)
{
    if (isCancelled()) return false;
    File file = source.open(lists_[index].path, FILE_READ);
    if (!file || file.isDirectory()) { file.close(); skip(); return true; }
    const size_t expected = file.size();
    if (expected > MaxPlaylistBytes) { file.close(); skip(); return true; }
    uint8_t buffer[512];
    char line[MaxPlaylistLine + 1];
    size_t used = 0, consumed = 0;
    bool overlong = false, first = true;
    while (consumed < expected) {
        if (isCancelled()) { file.close(); return false; }
        const size_t request = (std::min)(sizeof(buffer), expected - consumed);
        const size_t got = file.read(buffer, request);
        if (!got || got > request) { lists_[index].count = 0; skip(); file.close(); return true; }
        consumed += got;
        for (size_t i = 0; i < got; ++i) {
            const char c = static_cast<char>(buffer[i]);
            if (c == '\n') {
                if (isCancelled()) { file.close(); return false; }
                if (overlong) skip(); else playlistLine(index, line, used, first);
                first = false; used = 0; overlong = false;
            } else if (used < MaxPlaylistLine) line[used++] = c;
            else overlong = true;
        }
    }
    if (isCancelled()) { file.close(); return false; }
    if (overlong) skip(); else if (used) playlistLine(index, line, used, first);
    file.close();
    return true;
}

bool MediaCatalog::track(uint16_t id, char* path, size_t pathCapacity,
                         char* title, size_t titleCapacity) const
{
    if (!id || id > trackCount_ || !path || !title) return false;
    const Track& value = records_[id - 1];
    if (std::strlen(value.path) + 1 > pathCapacity || std::strlen(value.title) + 1 > titleCapacity) return false;
    std::strcpy(path, value.path); std::strcpy(title, value.title);
    return true;
}

bool MediaCatalog::tracks(uint16_t playlist, size_t offset, size_t limit, Page& out) const
{
    out = Page{};
    if (!limit || limit > MaxPage || playlist > playlistCount_) return false;
    out.total = playlist ? lists_[playlist - 1].count : trackCount_;
    if (offset >= out.total) return true;
    const size_t count = (std::min)(limit, out.total - offset);
    for (size_t i = 0; i < count; ++i) {
        const uint16_t id = playlist ? indices_[(playlist - 1) * MaxTracks + offset + i]
                                     : static_cast<uint16_t>(offset + i + 1);
        out.entries[i].id = id;
        std::strcpy(out.entries[i].title, records_[id - 1].title);
    }
    out.count = count;
    return true;
}

bool MediaCatalog::playlists(size_t offset, size_t limit, Page& out) const
{
    out = Page{};
    if (!limit || limit > MaxPage) return false;
    out.total = playlistCount_;
    if (offset >= out.total) return true;
    out.count = (std::min)(limit, out.total - offset);
    for (size_t i = 0; i < out.count; ++i) {
        out.entries[i].id = static_cast<uint16_t>(offset + i + 1);
        out.entries[i].count = lists_[offset + i].count;
        std::strcpy(out.entries[i].title, lists_[offset + i].title);
    }
    return true;
}
} // namespace meow::media
