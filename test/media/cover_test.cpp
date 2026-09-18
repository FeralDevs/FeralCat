#include "cover_loader.h"
#include "esp_heap_caps.h"
#include <lgfx/utility/lgfx_tjpgd.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace meow::media;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "FAIL line " << __LINE__ << ": " #x "\n"; std::exit(1); } } while(0)
static std::string fixture(const std::string& root, const char* name) {
    std::ifstream f(root + "/" + name, std::ios::binary); CHECK(bool(f));
    return std::string(std::istreambuf_iterator<char>(f), {});
}
struct Probe {
    mock::Disk disk;
    fs::FS fs{disk};
    std::array<uint16_t, CoverPixels + 2> buffer;
    CoverInfo info;
    Probe() { CHECK(mock::heap.live == 0); mock::heap = {}; buffer.fill(0xdead); }
    ~Probe() { CHECK(!disk.live); CHECK(!mock::heap.live); CHECK(!mock::heap.bytes); CHECK(!mock::heap.wrongCapabilities); CHECK(!disk.writes); CHECK(buffer.front() == 0xdead); CHECK(buffer.back() == 0xdead); }
    CoverResult load(const char* path = "/music/album/song.mp3", CoverHooks hooks = {}) { return loadTrackCover(fs, path, buffer.data() + 1, CoverPixels, info, hooks); }
    uint16_t pixel(size_t x, size_t y) { return buffer[1 + y * CoverWidth + x]; }
};
static void synchsafe(std::string& out, uint32_t n) { for (int s = 21; s >= 0; s -= 7) out += char((n >> s) & 127); }
static void big32(std::string& out, uint32_t n) { for (int s = 24; s >= 0; s -= 8) out += char(n >> s); }
static std::string tag(const std::string& jpeg, bool v4 = false, unsigned encoding = 0, const std::string& mime = "image/jpeg", unsigned picType = 3, bool ext = false) {
    std::string picture(1, char(encoding)); picture += mime; picture += '\0'; picture += char(picType);
    if (encoding == 1 || encoding == 2) picture += std::string("\xff\xfeN\0\0\0", 6);
    else picture += std::string("description\0", 12);
    picture += jpeg;
    std::string frames;
    if (ext) { if (v4) { synchsafe(frames, 6); frames += std::string("\1\0", 2); } else { big32(frames, 6); frames += std::string(6, '\0'); } }
    frames += "APIC";
    if (v4) synchsafe(frames, uint32_t(picture.size())); else big32(frames, uint32_t(picture.size()));
    frames += std::string(2, '\0'); frames += picture;
    std::string out("ID3", 3); out += char(v4 ? 4 : 3); out += '\0'; out += char(ext ? 0x40 : 0);
    synchsafe(out, uint32_t(frames.size())); out += frames; out += "FAKEAUDIO";
    return out;
}
static void marker(std::string& out, unsigned id, const std::string& data) {
    out += char(255); out += char(id); const unsigned n = unsigned(data.size()) + 2;
    out += char(n >> 8); out += char(n); out += data;
}
static std::string crafted(unsigned dc, unsigned ac, const std::string& bits, unsigned width = 8, unsigned quant = 1) {
    std::string out("\xff\xd8", 2), q(1, '\0'); q += std::string(64, char(quant)); marker(out, 0xdb, q);
    std::string frame; frame += char(8); frame += char(0); frame += char(8); frame += char(width >> 8); frame += char(width);
    frame += std::string("\1\1\x11\0", 4); marker(out, 0xc0, frame);
    for (unsigned cls = 0; cls < 2; ++cls) { std::string h(1, char(cls << 4)); h += char(1); h += std::string(15, '\0'); h += char(cls ? ac : dc); marker(out, 0xc4, h); }
    marker(out, 0xda, std::string("\1\1\0\0\x3f\0", 6));
    std::string padded = bits; while (padded.size() % 8) padded += '1';
    for (size_t pos = 0; pos < padded.size(); pos += 8) {
        unsigned byte = 0; for (size_t b = 0; b < 8; ++b) byte = byte * 2 + (padded[pos + b] == '1');
        out += char(byte); if (byte == 255) out += '\0';
    }
    out += std::string("\xff\xd9", 2); return out;
}
struct Direct { std::string data; size_t pos = 0; };
static uint32_t directIn(void* c, uint8_t* p, uint32_t n) {
    auto& d = *static_cast<Direct*>(c); n = uint32_t((std::min)(size_t(n), d.data.size() - d.pos));
    if (p) std::memcpy(p, d.data.data() + d.pos, n); d.pos += n; return n;
}
static uint32_t directOut(void*, void*, JRECT*) { return 1; }
static JRESULT rawDecode(const std::string& data) {
    Direct d{data}; lgfxJdec jd{}; alignas(8) std::array<uint8_t, 8192> pool{};
    const JRESULT prep = lgfx_jd_prepare(&jd, directIn, pool.data(), pool.size(), &d);
    CHECK(prep == JDR_OK); return lgfx_jd_decomp(&jd, directOut, 0);
}
struct Control { mock::Disk* disk; unsigned polls = 0, cancelAt = 0, tickStep = 0, tick = 0; size_t readsAt = SIZE_MAX; };
static bool cancel(void* c) { auto& p = *static_cast<Control*>(c); ++p.polls; return (p.cancelAt && p.polls >= p.cancelAt) || p.disk->reads >= p.readsAt; }
static uint32_t timeNow(void* c) { auto& p = *static_cast<Control*>(c); const auto value = p.tick; p.tick += p.tickStep; return value; }

int main(int argc, char** argv) {
    CHECK(argc == 2);
    const auto jpeg = fixture(argv[1], "cover-quadrants.jpg");
    const auto wide = fixture(argv[1], "cover-wide.jpg");
    const auto grey = fixture(argv[1], "cover-grey.jpg");
    const auto tiny = fixture(argv[1], "cover-tiny.jpg");
    const auto progressive = fixture(argv[1], "cover-progressive.jpg");
    const auto odd = fixture(argv[1], "cover-odd.jpg");
    const auto maximum = fixture(argv[1], "cover-maximum.jpg");
    {
        Probe p; p.disk.file("/music/album/cover.jpg", jpeg); p.disk.file("/music/album/folder.jpg", grey);
        CHECK(p.load() == CoverResult::Ready); CHECK(p.info.source == CoverSource::CoverJpeg); CHECK(p.info.width == 96 && p.info.height == 96); CHECK(p.disk.opens == 1);
        CHECK((p.pixel(20, 20) & 0xf800) >= 0xf000); CHECK((p.pixel(75, 20) & 0x7e0) >= 0x700);
        CHECK((p.pixel(20, 75) & 31) >= 28); CHECK(p.pixel(75, 75) >= 0xf7de);
        CHECK(mock::heap.peakBytes <= MaxCoverBytes + 8192); CHECK(p.disk.peakLive == 1);
    }
    {
        Probe p; p.disk.file("/music/album/cover.jpg", "broken"); p.disk.file("/music/album/folder.jpg", wide);
        CHECK(p.load() == CoverResult::Ready); CHECK(p.info.source == CoverSource::FolderJpeg); CHECK(p.info.width == 400 && p.info.height == 200);
        CHECK(p.pixel(30, 0) == 0); CHECK(p.pixel(30, 95) == 0); CHECK(p.pixel(30, 30) != 0);
    }
    for (const auto* image : {&grey, &tiny, &odd, &maximum}) { Probe p; p.disk.file("/music/album/cover.jpg", *image); CHECK(p.load() == CoverResult::Ready); }
    for (bool v4 : {false, true}) for (unsigned enc = 0; enc < 4; ++enc) for (bool ext : {false, true}) {
        Probe p; p.disk.file("/music/album/song.mp3", tag(jpeg, v4, enc, "image/jpeg", 3, ext));
        CHECK(p.load() == CoverResult::Ready); CHECK(p.info.source == CoverSource::EmbeddedJpeg); CHECK(p.disk.peakLive == 1);
    }
    {
        Probe p; p.disk.file("/music/album/song.mp3", tag(jpeg, true, 3, "image/jpg", 0)); CHECK(p.load() == CoverResult::Ready);
    }
    for (const auto& image : {progressive, std::string("", 0), std::string(MaxCoverBytes + 1, 'X'), jpeg.substr(0, jpeg.size() - 2)}) {
        Probe p; p.disk.file("/music/album/cover.jpg", image); CHECK(p.load() != CoverResult::Ready); CHECK(p.info.source == CoverSource::None);
    }
    for (const auto& image : {tag(jpeg, false, 0, "image/png"), tag(jpeg, false, 0, "-->"), tag(jpeg, false, 0, "image/jpeg", 4)}) {
        Probe p; p.disk.file("/music/album/song.mp3", image); CHECK(p.load() != CoverResult::Ready);
    }
    {
        Probe p; auto data = tag(jpeg); data[5] = char(0x80); p.disk.file("/music/album/song.mp3", data); CHECK(p.load() == CoverResult::Unsupported);
    }
    {
        Probe p; auto data = tag(jpeg); data[6] = char(0x80); p.disk.file("/music/album/song.mp3", data); CHECK(p.load() == CoverResult::Invalid);
    }
    {
        Probe p; auto data = tag(jpeg); data[14] = char(0x7f); p.disk.file("/music/album/song.mp3", data); CHECK(p.load() == CoverResult::Invalid);
    }
    {
        Probe p; auto data = tag(jpeg); std::string size; synchsafe(size, MaxCoverTagBytes + 1); data.replace(6, 4, size);
        p.disk.file("/music/album/song.mp3", data); CHECK(p.load() == CoverResult::Unsupported); CHECK(mock::heap.calls == 0); CHECK(p.disk.bytes == 10);
    }
    for (bool within : {false, true}) {
        Probe p; auto data = tag(jpeg); const unsigned frames = within ? 127 : 128;
        const std::string empty("TXXX\0\0\0\0\0\0", 10); std::string padding;
        for (unsigned i = 0; i < frames; ++i) padding += empty;
        data.insert(10, padding); std::string size; synchsafe(size, uint32_t(data.size() - 19)); data.replace(6, 4, size);
        p.disk.file("/music/album/song.mp3", data);
        CHECK((p.load() == CoverResult::Ready) == within);
        if (!within) { CHECK(p.disk.reads == 129); CHECK(mock::heap.calls == 0); }
    }
    {
        Probe p; auto data = tag(jpeg, false, 0, "image/jpeg", 3, true); data[10] = char(0x7f);
        p.disk.file("/music/album/song.mp3", data); CHECK(p.load() == CoverResult::Invalid);
    }
    {
        Probe p; auto data = jpeg; const size_t sof = data.find(std::string("\xff\xc0", 2)); CHECK(sof != std::string::npos);
        data[sof + 7] = char(0xff); data[sof + 8] = char(0xff); p.disk.file("/music/album/cover.jpg", data); CHECK(p.load() != CoverResult::Ready);
    }
    for (const char* path : {"/else/song.mp3", "/music/../../secret", "/music/album/../song.mp3", "/music/album/song\n.mp3", "/music", "C:/music/song.mp3"}) {
        Probe p; CHECK(p.load(path) == CoverResult::Invalid); CHECK(p.disk.io() == 0);
    }
    {
        Probe p; p.disk.file("/music/Grüße/cover.jpg", jpeg); CHECK(p.load("/music/Grüße/楽曲.mp3") == CoverResult::Ready);
    }
    for (size_t fail = 1; fail <= 2; ++fail) {
        Probe p; mock::heap.failCall = fail; p.disk.file("/music/album/cover.jpg", jpeg); CHECK(p.load() == CoverResult::NoMemory);
    }
    for (size_t fail = 1; fail <= 2; ++fail) {
        Probe p; mock::heap.failCall = fail; p.disk.file("/music/album/song.mp3", tag(jpeg)); CHECK(p.load() == CoverResult::NoMemory);
    }
    {
        Probe p; p.disk.readLimit = 7; p.disk.file("/music/album/cover.jpg", jpeg); CHECK(p.load() == CoverResult::Ready); CHECK(p.disk.reads > 20);
    }
    {
        Probe p; p.disk.failReadAt = 2; p.disk.file("/music/album/cover.jpg", jpeg); CHECK(p.load() != CoverResult::Ready);
    }
    for (unsigned poll : {1u, 3u, 8u, 20u, 35u, 55u}) {
        Probe p; p.disk.file("/music/album/cover.jpg", wide); Control c{&p.disk}; c.cancelAt = poll;
        CHECK(p.load("/music/album/song.mp3", {cancel, timeNow, &c}) == CoverResult::Cancelled);
    }
    {
        Probe p; p.disk.file("/music/album/song.mp3", tag(jpeg)); Control c{&p.disk}; c.readsAt = 3;
        CHECK(p.load("/music/album/song.mp3", {cancel, timeNow, &c}) == CoverResult::Cancelled);
    }
    {
        Probe p; p.disk.file("/music/album/cover.jpg", wide); Control c{&p.disk}; c.tickStep = 100;
        CHECK(p.load("/music/album/song.mp3", {cancel, timeNow, &c}) == CoverResult::TimedOut);
    }
    {
        Probe p; p.disk.file("/music/album/cover.jpg", wide); Control c{&p.disk}; c.tick = UINT32_MAX - 200; c.tickStep = 100;
        CHECK(p.load("/music/album/song.mp3", {cancel, timeNow, &c}) == CoverResult::TimedOut);
    }
    {
        Probe p; CHECK(p.load() == CoverResult::Missing); CHECK(p.disk.opens == 3); CHECK(p.disk.reads == 0); CHECK(mock::heap.calls == 0);
        CHECK(loadTrackCover(p.fs, "/music/a.mp3", nullptr, CoverPixels, p.info) == CoverResult::Invalid);
        CHECK(loadTrackCover(p.fs, "/music/a.mp3", p.buffer.data(), CoverPixels - 1, p.info) == CoverResult::Invalid);
    }
    // Exercise the real vendored entropy decoder, bypassing cover preflight.
    CHECK(rawDecode(crafted(0, 0, "00")) == JDR_OK);
    CHECK(rawDecode(crafted(0, 0xf1, "001010101")) == JDR_FMT1); // fourth zero-run indexes coefficient 64
    CHECK(rawDecode(crafted(11, 0, "0111111111110" "0111111111110", 16)) == JDR_FMT1); // accumulated DC4094
    CHECK(rawDecode(crafted(11, 0, "0111111111110", 8, 255)) == JDR_FMT1); // product exceeds int32 before old shift
    CHECK(rawDecode(crafted(12, 0, "01111111111110")) == JDR_FMT1);
    CHECK(rawDecode(crafted(0, 11, "0011111111111")) == JDR_FMT1);
    {
        auto aligned = crafted(0, 0, "00");
        const size_t sos = aligned.find(std::string("\xff\xda", 2));
        const size_t entropy = sos + 2 + (uint8_t(aligned[sos + 2]) << 8) + uint8_t(aligned[sos + 3]);
        size_t padding = 512 - entropy % 512; if (padding < 4) padding += 512;
        std::string app; marker(app, 0xe1, std::string(padding - 4, '\0')); aligned.insert(2, app);
        CHECK(rawDecode(aligned) == JDR_OK);
        Probe p; p.disk.file("/music/album/cover.jpg", aligned); CHECK(p.load() == CoverResult::Ready);
    }
    {
        Probe p; auto data = jpeg; std::string app; marker(app, 0xe1, "");
        for (unsigned i = 0; i < 128; ++i) data.insert(2, app);
        p.disk.file("/music/album/cover.jpg", data); CHECK(p.load() != CoverResult::Ready);
    }
    // Header truncations and deterministic corruptions exercise the production
    // preflight and decoder under the same allocator/file-lifetime assertions.
    for (size_t cut = 0; cut < jpeg.size(); cut += 7) { Probe p; p.disk.file("/music/album/cover.jpg", jpeg.substr(0, cut)); CHECK(p.load() != CoverResult::Ready); }
    uint32_t seed = 0x12345678;
    for (unsigned i = 0; i < 500; ++i) {
        auto mutated = jpeg;
        for (unsigned j = 0; j < 4; ++j) { seed = seed * 1664525 + 1013904223; const size_t offset = seed % mutated.size(); seed = seed * 1664525 + 1013904223; mutated[offset] = char(seed >> 24); }
        Probe p; p.disk.file("/music/album/cover.jpg", mutated); (void)p.load();
    }
    std::cout << "Cover: " << checks << " checks passed\n";
}
