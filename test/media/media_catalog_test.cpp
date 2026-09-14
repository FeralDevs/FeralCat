#include "media_catalog.h"
#include "esp_heap_caps.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using namespace meow::media;
static size_t checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "%s:%d failed: %s\n", __FILE__, __LINE__, #condition); std::exit(1); } } while (false)

static bool normalize(const std::string& base, const std::string& text, std::string* out = nullptr)
{
    char result[192];
    const bool okay = normalizeMediaPath(base.c_str(), text.data(), text.size(), result, sizeof(result));
    if (out) *out = result;
    return okay;
}
static std::string number(size_t n)
{
    char value[24]; std::snprintf(value, sizeof(value), "%04u", static_cast<unsigned>(n)); return value;
}
static uint16_t idFor(const MediaCatalog& catalog, const std::string& wanted)
{
    char path[192], title[96];
    for (uint16_t id = 1; id <= catalog.trackCount(); ++id) {
        CHECK(catalog.track(id, path, sizeof(path), title, sizeof(title)));
        if (path == wanted) return id;
    }
    return 0;
}
static void pathTests()
{
    std::string value;
    CHECK(normalize("/mp3/album", "..\\mix/./Song.MP3", &value));
    CHECK(value == "/mp3/mix/Song.MP3");
    CHECK(normalize("/mp3/a/b", "../../track.mp3", &value) && value == "/mp3/track.mp3");
    CHECK(normalize("/mp3/ignored", "/MP3//a/../b.mp3", &value) && value == "/mp3/b.mp3");
    const std::vector<std::string> bad = {"", "../outside.mp3", "a/../../outside.mp3",
      "/mp30/track.mp3", "/music/track.mp3", "/mp3/../mp3/track.mp3",
      "https://example/a.mp3", "C:\\mp3\\a.mp3", "file:///mp3/a.mp3",
      "a:b.mp3", "\\\\server\\mp3\\a.mp3", std::string("a\0.mp3", 6),
      std::string("\xc0\xae\xc0\xae/a.mp3", 10), std::string("\xed\xa0\x80.mp3", 7),
      std::string("\xf4\x90\x80\x80.mp3", 8), std::string("\xc2\x85.mp3", 6),
      std::string("\xe2\x82", 2), "a\t.mp3", "a\n.mp3", std::string("\x7f.mp3", 5)};
    for (const auto& text : bad) CHECK(!normalize("/mp3", text));
    CHECK(!normalize("/outside", "track.mp3"));
    CHECK(!normalize("/mp3/../outside", "track.mp3"));
    const std::string unicode = "Gr\xc3\xbc\xc3\x9f" "e-\xf0\x9f\x90\xb1.mp3";
    CHECK(normalize("/mp3", unicode, &value) && value == "/mp3/" + unicode);
    CHECK(validMediaText(unicode.data(), unicode.size()));
    CHECK(normalize("/mp3", std::string(182, 'a') + ".mp3", &value) && value.size() == 191);
    CHECK(!normalize("/mp3", std::string(183, 'a') + ".mp3"));
    char tiny[4] = "x";
    CHECK(!normalizeMediaPath("/mp3", "a.mp3", 5, tiny, sizeof(tiny)) && tiny[0] == 0);
    CHECK(!normalizeMediaPath("/mp3", "a", 1, nullptr, 0));
    CHECK(compareMediaPaths("/mp3/A.mp3", "/MP3/a.MP3") == 0);
    CHECK(compareMediaPaths("/mp3/b.mp3", "/mp3/A.mp3") > 0);
}

static void scanAndPlaylistTests()
{
    mock::Disk disk;
    disk.file("/mp3/Zulu.mp3"); disk.file("/mp3/alpha.MP3");
    disk.file("/mp3/album/beta.mp3"); disk.file("/mp3/note.txt");
    disk.file("/mp3/d1/d2/d3/d4/allowed.mp3");
    disk.file("/mp3/d1/d2/d3/d4/d5/too-deep.mp3");
    const std::string longStem = std::string(94, 'a') + "\xf0\x9f\x90\xb1";
    disk.file("/mp3/" + longStem + ".mp3");
    std::string playlist = "\xef\xbb\xbf#EXTM3U\r\n#EXTINF:7,ignored title\r\n"
      " beta.mp3 \r\n../Zulu.mp3\r\n/MP3/alpha.MP3\n.\\beta.mp3\n"
      "missing.mp3\nhttps://example/a.mp3\n../../escape.mp3\nC:\\mp3\\a.mp3\n"
      "../other.m3u\n";
    playlist += std::string("bad\0.mp3", 8) + "\n";
    playlist += std::string("\xed\xa0\x80.mp3", 7) + "\n";
    playlist += std::string(256, 'x') + "\n";
    playlist += "../alpha.MP3"; // Last line without newline.
    disk.file("/mp3/album/Mix.m3u8", playlist);
    disk.file("/mp3/A.m3u", "alpha.MP3\n");
    fs::FS source(disk); MediaCatalog catalog;
    CHECK(catalog.scan(source) == MediaCatalog::ScanResult::Complete);
    CHECK(catalog.trackCount() == 5 && catalog.playlistCount() == 2);
    CHECK(catalog.skipped() == 9); // Eight invalid entries plus the fifth subfolder.
    CHECK(disk.live == 0 && disk.peakLive <= 6 && disk.writes == 0);
    CHECK(mock::heap.live == 3 && !mock::heap.wrongCapabilities);
    CHECK(mock::heap.bytes < 175000); // Bounded records + playlist indices, all PSRAM.
    std::string previous;
    char path[192], title[96];
    for (uint16_t id = 1; id <= catalog.trackCount(); ++id) {
        CHECK(catalog.track(id, path, sizeof(path), title, sizeof(title)));
        CHECK(previous.empty() || compareMediaPaths(previous.c_str(), path) < 0);
        CHECK(validMediaText(title, std::strlen(title)));
        previous = path;
        if (std::string(path).find(longStem) != std::string::npos) CHECK(std::strlen(title) == 94);
    }
    const uint16_t beta = idFor(catalog, "/mp3/album/beta.mp3");
    const uint16_t zulu = idFor(catalog, "/mp3/Zulu.mp3");
    const uint16_t alpha = idFor(catalog, "/mp3/alpha.MP3");
    const size_t before = disk.io();
    Page page;
    CHECK(catalog.playlists(0, 8, page) && page.total == 2 && page.count == 2);
    CHECK(std::string(page.entries[0].title) == "A" && page.entries[0].count == 1);
    CHECK(std::string(page.entries[1].title) == "Mix" && page.entries[1].count == 5);
    CHECK(catalog.tracks(2, 0, 8, page) && page.count == 5);
    const uint16_t expected[] = {beta, zulu, alpha, beta, alpha};
    for (size_t i = 0; i < 5; ++i) CHECK(page.entries[i].id == expected[i]);
    CHECK(catalog.tracks(2, 2, 2, page) && page.count == 2 && page.entries[0].id == alpha);
    CHECK(catalog.tracks(0, 4, 8, page) && page.count == 1 && page.entries[0].id == 5);
    CHECK(catalog.tracks(0, SIZE_MAX, 8, page) && page.count == 0);
    CHECK(!catalog.tracks(0, 0, 9, page) && !catalog.tracks(0, 0, 0, page));
    CHECK(!catalog.tracks(3, 0, 8, page));
    CHECK(catalog.playlists(SIZE_MAX, 8, page) && page.count == 0);
    CHECK(!catalog.playlists(0, 9, page) && !catalog.playlists(0, 0, page));
    CHECK(!catalog.track(0, path, sizeof(path), title, sizeof(title)));
    CHECK(!catalog.track(1, path, 1, title, sizeof(title)));
    CHECK(!catalog.track(1, path, sizeof(path), title, 1));
    CHECK(disk.io() == before); // Every getter and page uses only published memory.
}

static void limitsTests()
{
    {
        mock::Disk disk;
        for (size_t i = 0; i < 520; ++i) disk.file("/mp3/" + number(519 - i) + ".mp3");
        std::string entries;
        for (size_t i = 0; i < 515; ++i) entries += "0000.mp3\n";
        for (size_t i = 0; i < 17; ++i) disk.file("/mp3/" + number(i) + ".m3u", entries);
        fs::FS source(disk); MediaCatalog catalog;
        CHECK(catalog.scan(source) == MediaCatalog::ScanResult::Complete);
        CHECK(catalog.trackCount() == 512 && catalog.playlistCount() == 16);
        char path[192], title[96];
        CHECK(catalog.track(1, path, sizeof(path), title, sizeof(title)) && std::string(path) == "/mp3/0000.mp3");
        CHECK(catalog.track(512, path, sizeof(path), title, sizeof(title)) && std::string(path) == "/mp3/0511.mp3");
        CHECK(catalog.skipped() == 8 + 1 + 16 * 3);
        Page page;
        CHECK(catalog.tracks(1, 504, 8, page) && page.count == 8 && page.total == 512);
        CHECK(catalog.playlists(8, 8, page) && page.count == 8 && page.total == 16);
        CHECK(disk.live == 0 && disk.writes == 0);
        // Reversing the same directory must not change retained paths or IDs.
        std::reverse(disk.nodes["/mp3"].children.begin(), disk.nodes["/mp3"].children.end());
        CHECK(catalog.scan(source) == MediaCatalog::ScanResult::Complete);
        CHECK(catalog.track(1, path, sizeof(path), title, sizeof(title)) && std::string(path) == "/mp3/0000.mp3");
        CHECK(catalog.track(512, path, sizeof(path), title, sizeof(title)) && std::string(path) == "/mp3/0511.mp3");
        CHECK(catalog.playlists(0, 1, page) && std::string(page.entries[0].title) == "0000");
        CHECK(catalog.playlists(15, 1, page) && std::string(page.entries[0].title) == "0015");
    }
    {
        mock::Disk disk; disk.file("/mp3/a.mp3");
        disk.file("/mp3/large.m3u", std::string(MaxPlaylistBytes + 1, '\n'));
        fs::FS source(disk); MediaCatalog catalog;
        CHECK(catalog.scan(source) == MediaCatalog::ScanResult::Complete);
        CHECK(catalog.skipped() == 1 && disk.reads == 0 && disk.live == 0);
        Page page; CHECK(catalog.tracks(1, 0, 8, page) && page.total == 0);
    }
    {
        mock::Disk disk; disk.file("/mp3/a.mp3");
        std::string exact(MaxPlaylistBytes - 6, '\n'); exact += "a.mp3\n";
        disk.file("/mp3/exact.m3u", exact);
        fs::FS source(disk); MediaCatalog catalog;
        CHECK(catalog.scan(source) == MediaCatalog::ScanResult::Complete);
        CHECK(catalog.skipped() == 0 && disk.bytes == MaxPlaylistBytes && disk.live == 0);
        Page page; CHECK(catalog.tracks(1, 0, 8, page) && page.total == 1);
    }
    {
        mock::Disk disk;
        for (size_t i = 0; i < 4100; ++i) disk.file("/mp3/" + number(i) + ".txt");
        fs::FS source(disk); MediaCatalog catalog;
        CHECK(catalog.scan(source) == MediaCatalog::ScanResult::Complete);
        CHECK(disk.entries == 4096 && disk.nextCalls == 4096 && catalog.skipped() == 1);
        CHECK(disk.live == 0);
    }
    {
        mock::Disk disk; disk.file("/mp3/valid.mp3");
        disk.file("/mp3/bad.mp3"); disk.nodes["/mp3/bad.mp3"].leafOverride = "../outside.mp3";
        disk.file("/mp3/A.mp3"); disk.file("/mp3/a.MP3");
        fs::FS source(disk); MediaCatalog catalog;
        CHECK(catalog.scan(source) == MediaCatalog::ScanResult::Complete);
        CHECK(catalog.trackCount() == 2 && catalog.skipped() == 2 && disk.live == 0);
    }
}

struct CancelState { mock::Disk* disk; size_t entries = SIZE_MAX, reads = SIZE_MAX, calls = 0, afterCalls = SIZE_MAX; };
static bool cancel(void* value)
{
    auto& state = *static_cast<CancelState*>(value); ++state.calls;
    return state.calls >= state.afterCalls || state.disk->entries >= state.entries || state.disk->reads >= state.reads;
}
static void cancellationAndFailureTests()
{
    mock::Disk disk; disk.file("/mp3/a.mp3");
    std::string entries;
    for (size_t i = 0; i < 1000; ++i) entries += "a.mp3\n";
    disk.file("/mp3/mix.m3u", entries);
    fs::FS source(disk);
    {
        MediaCatalog previous;
        CHECK(previous.scan(source) == MediaCatalog::ScanResult::Complete);
        const size_t allocated = mock::heap.live;
        for (int mode = 0; mode < 3; ++mode) {
            disk.entries = disk.reads = 0;
            CancelState state{&disk};
            if (mode == 0) state.afterCalls = 1;
            if (mode == 1) state.entries = 1;
            if (mode == 2) state.reads = 1;
            MediaCatalog candidate;
            CHECK(candidate.scan(source, cancel, &state) == MediaCatalog::ScanResult::Cancelled);
            CHECK(candidate.trackCount() == 0 && candidate.playlistCount() == 0 && disk.live == 0);
            CHECK(mock::heap.live == allocated && previous.trackCount() == 1);
            if (mode == 1) CHECK(disk.entries == 1);
            if (mode == 2) CHECK(disk.reads == 1); // Cancel is observed while handling the first line.
        }
    }
    CHECK(mock::heap.live == 0);
    for (size_t failure = 1; failure <= 3; ++failure) {
        mock::heap = {}; mock::heap.failCall = failure;
        MediaCatalog catalog;
        CHECK(catalog.scan(source) == MediaCatalog::ScanResult::Error);
        CHECK(mock::heap.live == 0 && catalog.trackCount() == 0);
        CHECK(std::strstr(catalog.error(), "PSRAM") != nullptr);
    }
    mock::heap = {};
    {
        mock::Disk missing; fs::FS sourceMissing(missing); MediaCatalog catalog;
        CHECK(catalog.scan(sourceMissing) == MediaCatalog::ScanResult::Error);
        CHECK(std::strstr(catalog.error(), "/mp3") && mock::heap.live == 0 && missing.live == 0);
    }
    {
        disk.reads = 0; disk.readLimit = 3; disk.failReadAt = SIZE_MAX;
        MediaCatalog catalog;
        CHECK(catalog.scan(source) == MediaCatalog::ScanResult::Complete);
        Page page; CHECK(catalog.tracks(1, 0, 8, page) && page.total == 512);
        CHECK(disk.live == 0 && disk.reads == entries.size() / 3);
        disk.reads = 0; disk.failReadAt = 4;
        CHECK(catalog.scan(source) == MediaCatalog::ScanResult::Complete);
        CHECK(catalog.tracks(1, 0, 8, page) && page.total == 0);
        CHECK(catalog.skipped() == 1 && disk.live == 0);
    }
    CHECK(mock::heap.live == 0);
}
int main()
{
    pathTests(); scanAndPlaylistTests(); CHECK(mock::heap.live == 0);
    limitsTests(); CHECK(mock::heap.live == 0);
    cancellationAndFailureTests();
    std::printf("Media catalog: %zu checks passed\n", checks);
}
