// Run the shipped Lua app in the real sandbox with asynchronous audio snapshots.
#include "lua_runtime.h"
#include "app_manifest.h"
#include "media_api.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {
using meow::luaapps::Runtime;
using meow::luaapps::View;
using meow::luaapps::ViewKind;
using meow::media::Action;
using meow::media::Command;
using meow::media::Page;
using meow::media::Status;
int checks = 0;
std::size_t maxVmBytes = 0;

void require(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

std::string read(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "packaged source is readable");
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool validUtf8(const char* text) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(text);
    const auto length = std::strlen(text);
    for (std::size_t i = 0; i < length;) {
        unsigned b = bytes[i], n = 1;
        if (b >= 0xc2 && b <= 0xdf) n = 2;
        else if (b >= 0xe0 && b <= 0xef) n = 3;
        else if (b >= 0xf0 && b <= 0xf4) n = 4;
        else if (b >= 0x80) return false;
        if (i + n > length) return false;
        for (unsigned j = 1; j < n; ++j) if (bytes[i+j] < 0x80 || bytes[i+j] > 0xbf) return false;
        if (n > 1 && ((b == 0xe0 && bytes[i+1] < 0xa0) || (b == 0xed && bytes[i+1] >= 0xa0) ||
                      (b == 0xf0 && bytes[i+1] < 0x90) || (b == 0xf4 && bytes[i+1] >= 0x90))) return false;
        i += n;
    }
    return true;
}

struct Audio : meow::media::Host {
    struct List { std::string title; std::vector<uint16_t> ids; };
    Status snapshot;
    std::vector<std::string> titles;
    std::vector<List> lists;
    std::vector<Command> commands;
    std::size_t applied = 0, trackReads = 0, listReads = 0;
    std::size_t queueCapacity = static_cast<std::size_t>(-1);
    bool accept = true, busy = false, pageBusy = false;

    explicit Audio(unsigned tracks = 12) {
        for (unsigned i = 1; i <= tracks; ++i) titles.push_back("Track " + std::to_string(i));
        snapshot.tracks = uint16_t(tracks);
        snapshot.generation = 1;
        snapshot.volume = 35;
    }
    void list(const char* title, std::vector<uint16_t> ids) {
        lists.push_back({title, std::move(ids)});
        snapshot.playlists = uint16_t(lists.size());
    }
    void state(const char* value) { std::snprintf(snapshot.state, sizeof(snapshot.state), "%s", value); }
    bool command(Command value) override {
        if (!accept || commands.size() - applied >= queueCapacity) return false;
        commands.push_back(value);
        return true; // Deliberately leave snapshot unchanged until apply().
    }
    bool status(Status& out) override { if (busy) return false; out = snapshot; return true; }
    bool tracks(uint16_t playlist, std::size_t offset, std::size_t limit, Page& out) override {
        ++trackReads;
        require(limit >= 1 && limit <= 8, "app requests bounded track pages");
        if (pageBusy) return false;
        out = {};
        if (playlist > lists.size()) return true;
        out.total = playlist ? lists[playlist-1].ids.size() : titles.size();
        for (std::size_t i = offset; i < out.total && out.count < limit; ++i) {
            const uint16_t id = playlist ? lists[playlist-1].ids[i] : uint16_t(i+1);
            auto& entry = out.entries[out.count++];
            entry.id = id;
            std::snprintf(entry.title, sizeof(entry.title), "%s", titles.at(id-1).c_str());
        }
        return true;
    }
    bool playlists(std::size_t offset, std::size_t limit, Page& out) override {
        ++listReads;
        require(limit >= 1 && limit <= 8, "app requests bounded playlist pages");
        if (pageBusy) return false;
        out = {};
        out.total = lists.size();
        for (std::size_t i = offset; i < lists.size() && out.count < limit; ++i) {
            auto& entry = out.entries[out.count++];
            entry.id = uint16_t(i+1);
            entry.count = uint16_t(lists[i].ids.size());
            std::snprintf(entry.title, sizeof(entry.title), "%s", lists[i].title.c_str());
        }
        return true;
    }
    void apply(std::size_t limit = static_cast<std::size_t>(-1)) {
        while (applied < commands.size() && limit-- > 0) {
            const auto command = commands[applied++];
            switch (command.action) {
            case Action::Play:
                ++snapshot.playback;
                snapshot.track = uint16_t(command.value);
                snapshot.position = 0; snapshot.duration = 180; snapshot.error[0] = 0;
                std::snprintf(snapshot.title, sizeof(snapshot.title), "%s", titles.at(command.value-1).c_str());
                state("playing"); break;
            case Action::Pause:
                state(!std::strcmp(snapshot.state, "paused") ? "playing" : "paused"); break;
            case Action::Stop: state("stopped"); break;
            case Action::Volume: snapshot.volume = uint8_t(command.value); break;
            case Action::Seek: snapshot.position = uint32_t(command.value); break;
            case Action::Equalizer: snapshot.equalizer = uint8_t(command.value); break;
            case Action::Scan: state("scanning"); break;
            }
        }
    }
    void finish() { ++snapshot.finished; snapshot.position = snapshot.duration; state("ended"); }
    std::vector<int> plays() const {
        std::vector<int> result;
        for (const auto& command : commands) if (command.action == Action::Play) result.push_back(command.value);
        return result;
    }
    Command last() const { require(!commands.empty(), "audio command was issued"); return commands.back(); }
};

struct Host : meow::luaapps::Host {
    Audio service;
    View view;
    unsigned exits = 0, shows = 0;
    bool backCaptured = false;
    explicit Host(unsigned count) : service(count) {}
    void show(const View& value) override {
        view = value; ++shows;
        require(view.itemCount <= 8, "app respects eight-action view limit");
        require(view.kind != ViewKind::List, "player app uses large native layouts only");
        if (view.kind == ViewKind::Player) {
            require(view.itemCount == 5, "main view has three transport and two navigation actions");
            require(validUtf8(view.player.title) && validUtf8(view.player.subtitle) && validUtf8(view.player.status), "player metadata is complete UTF-8");
        } else require(view.itemCount <= 4, "every submenu fits at most four large actions");
        require(validUtf8(view.title) && validUtf8(view.body), "display text is complete UTF-8");
        for (std::size_t i = 0; i < view.itemCount; ++i)
            require(validUtf8(view.items[i].label), "action label ends on UTF-8 boundary");
    }
    uint32_t uptimeMs() override { return 32100; }
    void requestExit() override { ++exits; }
    meow::media::Host* audio() override { return &service; }
    void captureBack(bool enabled) override { backCaptured = enabled; }
};

const std::string packagePath = std::string(MEOW_TEST_ROOT) + "/sd files/apps/mp3_player/";
std::string source;

struct HeapStats {
    std::size_t live = 0, peak = 0, calls = 0, allocatedBytes = 0;
    static void* resize(void* context, void* pointer, std::size_t oldSize, std::size_t newSize) {
        auto& stats = *static_cast<HeapStats*>(context);
        ++stats.calls;
        if (newSize == 0) { std::free(pointer); stats.live -= oldSize; return nullptr; }
        void* value = std::realloc(pointer, newSize);
        if (value) {
            stats.live = stats.live - oldSize + newSize;
            stats.peak = (std::max)(stats.peak, stats.live);
            if (newSize > oldSize) stats.allocatedBytes += newSize - oldSize;
        }
        return value;
    }
};

struct Player {
    Host host;
    HeapStats heap;
    Runtime runtime;
    explicit Player(unsigned count = 12, std::size_t quota = 256 * 1024)
        : host(count), runtime(host, [&]() { meow::luaapps::Limits limits; limits.memoryBytes = quota; return limits; }(),
                             {&heap, HeapStats::resize}) {}
    void start() {
        require(runtime.start(source.data(), source.size()), runtime.error());
        require(host.backCaptured, "player captures short Back for nested navigation");
        require(host.view.kind == ViewKind::Player, "main transport requests native player layout");
        require(!has("close"), "main player has no accidental Close action");
        memory();
    }
    void memory() {
        maxVmBytes = (std::max)(maxVmBytes, runtime.memoryUsed());
        require(runtime.memoryUsed() <= 256 * 1024, "VM remains below manifest memory quota");
    }
    bool has(const char* id) const {
        for (std::size_t i = 0; i < host.view.itemCount; ++i)
            if (!std::strcmp(host.view.items[i].id, id)) return true;
        return false;
    }
    void action(const char* id) {
        require(has(id), id);
        require(runtime.event("action", id), runtime.error()); memory();
    }
    void key(const char* id) { require(runtime.event("key", id), runtime.error()); memory(); }
    void tick(unsigned frames = 4) {
        for (unsigned i = 0; i < frames; ++i) {
            const auto reads = host.service.trackReads;
            require(runtime.tick(50), runtime.error());
            require(host.service.trackReads - reads <= 1, "queue build reads at most one page per callback");
            memory();
        }
    }
    void acknowledge() { host.service.apply(); tick(); }
    void loadQueue() {
        const auto before = host.service.plays().size();
        for (unsigned i = 0; i < 70 && host.service.plays().size() == before; ++i) tick(1);
        require(host.service.plays().size() == before + 1, "queue loads then requests one play");
    }
    void playAll() { action("play"); loadQueue(); acknowledge(); }
    void eof() { host.service.finish(); tick(); }
    const char* message() const { return host.view.kind == ViewKind::Player ? host.view.player.status : host.view.body; }
    void main() {
        for (unsigned i = 0; i < 6 && host.view.kind != ViewKind::Player; ++i) key("back");
        require(host.view.kind == ViewKind::Player, "Back returns to player without stopping playback");
    }
    void options() { main(); action("sound"); action("options"); }
    void seek() { options(); action("seek"); }
    void volume() {
        main(); action("sound"); action("volume");
        require(host.view.kind == ViewKind::Menu && host.view.compact && host.view.itemCount == 3,
                "volume requests large 2x2 grid with quieter, louder and back");
    }
    void scanMenu() { main(); action("library"); action("library_options"); }
};

void packagedManifest() {
    auto text = read(packagePath + "manifest.ini");
    source = read(packagePath + "main.lua");
    meow::luaapps::Manifest manifest{}; char error[192]{};
    require(meow::luaapps::parseManifest(text.data(), text.size(), manifest, error, sizeof(error)), error);
    require(!std::strcmp(manifest.id, "mp3_player") && !std::strcmp(manifest.name, "MP3 Player"), "standalone MP3 package identity");
    require(manifest.memoryBytes == 256 * 1024 && manifest.musicIcon &&
            (manifest.capabilities & meow::luaapps::CapAudio), "package declares memory, audio and music icon");
    require(source.size() <= meow::luaapps::kScriptMaxBytes, "packaged script fits text-source bound");
}

void transportAndStaleSnapshots() {
    Player p; p.start(); p.playAll();
    require(p.host.service.plays() == std::vector<int>{1}, "starts first track of all-track queue");
    p.action("next"); p.action("next");
    require(p.host.service.plays() == std::vector<int>({1,2,3}), "rapid Next follows requested position, not stale playing track");
    p.host.service.finish(); p.tick(); // Previous track finishes before queued Next is processed.
    require(p.host.service.plays().size() == 3, "old EOF cannot skip new pending track");
    p.acknowledge(); p.eof();
    require(p.host.service.last().action == Action::Play && p.host.service.last().value == 4, "natural EOF advances acknowledged track once");
    p.tick(20);
    require(p.host.service.plays().size() == 4, "repeated ended snapshots do not repeat advancement");
    p.acknowledge(); p.action("previous"); p.acknowledge();
    require(p.host.service.snapshot.track == 3, "previous selects preceding queue entry");
    p.action("play"); require(p.host.service.last().action == Action::Pause, "Play button pauses active playback");
    p.acknowledge(); p.action("play"); p.acknowledge();
    require(!std::strcmp(p.host.service.snapshot.state, "playing"), "Pause button resumes through toggle");
    const auto plays = p.host.service.plays().size();
    p.action("library"); p.action("all"); p.action("page_next");
    require(std::strstr(p.host.view.body, "3-4 of 12"), "track list supports next page");
    p.key("left"); p.key("back"); p.key("back");
    require(!std::strcmp(p.host.view.title, "MP3 Player") && p.host.service.plays().size() == plays,
            "browsing and Back preserve current playback");
    p.seek(); p.action("stop");
    p.host.service.finish(); p.tick();
    require(p.host.service.plays().size() == plays, "explicit Stop disarms delayed EOF advancement");
    p.main(); p.action("play");
    require(p.host.service.last().action == Action::Play && p.host.service.last().value == 3,
            "Play after accepted Stop restarts queue entry despite stale state");
    p.scanMenu(); p.action("close");
    require(p.host.exits == 0, "Close first opens explicit confirmation");
    p.action("confirm_close"); require(p.host.exits == 1, "confirmed Close exits app");
    p.runtime.stop();
    require(p.host.service.last().action == Action::Stop && p.runtime.memoryUsed() == 0, "app shutdown requests Stop and releases VM");
}

void playlistOrderAndRepeat() {
    Player p(8); p.host.service.list("Favorites", {6,2,6}); p.start();
    p.action("library"); p.action("lists"); p.action("pick1"); p.action("pick2");
    p.loadQueue(); p.acknowledge();
    require(p.host.service.snapshot.track == 2, "selected M3U row chooses playlist position");
    p.eof(); p.acknowledge();
    require(p.host.service.snapshot.track == 6, "M3U preserves order and duplicate IDs");
    const auto count = p.host.service.plays().size(); p.eof();
    require(p.host.service.plays().size() == count, "Repeat Off stops at playlist end");
    p.options(); p.action("repeat"); // Repeat All.
    p.main(); p.action("play"); p.acknowledge(); p.eof(); p.acknowledge();
    require(p.host.service.snapshot.track == 6, "Repeat All wraps to first M3U position");
    p.eof(); p.acknowledge();
    require(p.host.service.snapshot.track == 2, "wrapped list retains original M3U order");
    p.options(); p.action("repeat"); p.main(); // Repeat One.
    p.eof(); p.acknowledge();
    require(p.host.service.snapshot.track == 2, "Repeat One replays current track on natural EOF");
    p.action("next"); p.acknowledge();
    require(p.host.service.snapshot.track == 6, "manual Next overrides Repeat One");
    p.key("back"); require(p.host.exits == 0 && std::strstr(p.message(), "Hold B"), "short Back on main only shows hold instruction");
}

void shuffleAndLargeQueue() {
    Player p(512, 192 * 1024); p.start(); p.options(); p.action("shuffle"); p.main();
    p.playAll();
    std::set<int> seen{p.host.service.snapshot.track};
    for (unsigned i = 1; i < 512; ++i) {
        p.eof(); p.acknowledge(); seen.insert(p.host.service.snapshot.track);
    }
    require(seen.size() == 512, "shuffle visits every queue position once before end");
    const auto count = p.host.service.plays().size(); p.eof();
    require(p.host.service.plays().size() == count, "shuffle with Repeat Off finishes after complete queue");
    p.options(); p.action("shuffle"); p.main();
    p.action("next"); p.acknowledge();
    require(p.host.service.snapshot.track == (*p.host.service.plays().rbegin()), "shuffle toggle retains valid queue position");
    require(p.runtime.memoryUsed() < 192 * 1024, "512-entry queue fits tighter 192 KiB test quota");
}

void controlsAndBusy() {
    Player p; p.start();
    p.host.service.pageBusy = true; p.action("library"); p.action("all");
    require(p.host.view.itemCount == 1 && p.has("back"), "busy page exposes no stale selection");
    p.tick(12); p.host.service.pageBusy = false; p.tick();
    require(p.has("pick1"), "busy page fetch retries on later ticks");
    p.action("pick1"); p.loadQueue(); p.acknowledge();
    p.host.service.busy = true; p.tick(20); p.host.service.busy = false;
    require(p.runtime.active(), "temporarily busy status does not fault app");
    p.host.service.accept = false; p.action("next");
    require(p.host.service.plays().size() == 1 && std::strstr(p.message(), "Busy"), "rejected command reports busy without changing queue");
    p.host.service.accept = true; p.action("next");
    require(p.host.service.last().value == 2, "retry after rejection selects correct next position");
    p.acknowledge();
    p.volume();
    for (unsigned i = 0; i < 25; ++i) p.action("louder");
    require(p.host.service.last().action == Action::Volume && p.host.service.last().value == 100, "rapid volume increases use local value and clamp at 100");
    for (unsigned i = 0; i < 25; ++i) p.action("quieter");
    require(p.host.service.last().value == 0, "volume clamps at zero");
    p.acknowledge(); p.host.service.snapshot.position = 25; p.tick();
    p.seek(); p.action("seek_forward"); p.action("seek_forward");
    require(p.host.service.last().action == Action::Seek && p.host.service.last().value == 45, "rapid seeks use absolute queued target");
    for (unsigned i = 0; i < 10; ++i) p.action("seek_back");
    require(p.host.service.last().value == 0, "seek clamps at start");
    p.acknowledge(); p.host.service.snapshot.position = 175; p.tick(); p.action("seek_forward");
    require(p.host.service.last().value == 180, "seek clamps to known duration");
}

void errorsAndScanGeneration() {
    Player p(3); p.start(); p.playAll();
    p.host.service.state("error"); std::snprintf(p.host.service.snapshot.error, sizeof(p.host.service.snapshot.error), "%s", "File removed");
    p.tick(20);
    require(p.host.service.plays().size() == 1 && std::strstr(p.message(), "File removed"), "decoder error stays visible without auto skip");
    p.action("play"); p.tick(); // Retried same ID, old error snapshot remains.
    p.acknowledge(); p.eof();
    require(p.host.service.last().action == Action::Play && p.host.service.last().value == 2, "retry survives stale error and rearms EOF advancement");
    p.acknowledge();
    p.scanMenu(); p.action("scan"); p.host.service.finish(); p.tick();
    const auto count = p.host.service.plays().size();
    require(p.host.service.last().action == Action::Scan, "rescan disarms old EOF before worker acknowledgement");
    p.host.service.apply(); ++p.host.service.snapshot.generation; p.host.service.state("idle");
    p.host.service.titles = {"New track"}; p.host.service.snapshot.tracks = 1;
    p.tick(); p.main(); p.action("next");
    require(p.host.service.plays().size() == count, "scan generation invalidates old track IDs and queue");
    p.action("play"); p.loadQueue(); p.acknowledge();
    require(p.host.service.snapshot.track == 1, "new generation can load a fresh queue");

    Player empty(0); empty.start();
    require(empty.host.service.last().action == Action::Scan, "empty startup requests media scan");
    empty.host.service.apply(); ++empty.host.service.snapshot.generation; empty.host.service.state("idle"); empty.tick();
    empty.action("play"); empty.tick(20);
    require(std::strstr(empty.message(), "No playable tracks"), "empty queue produces useful explanation");
}

void seekWaitsForDuration() {
    Player p(3); p.start(); p.playAll();
    p.host.service.snapshot.duration = 0;
    p.tick(); p.seek();
    const auto commands = p.host.service.commands.size();
    p.action("seek_forward"); p.action("seek_back");
    require(p.host.service.commands.size() == commands, "unknown duration sends neither forward nor backward seek");
    require(std::strstr(p.host.view.body, "Duration not known yet"), "unknown duration reports wait hint");
    require(!std::strcmp(p.host.service.snapshot.state, "playing"), "unknown-duration seek leaves playback running");
    p.host.service.state("paused"); p.tick(); p.action("seek_forward");
    require(p.host.service.commands.size() == commands, "paused track with unknown duration also sends no seek");
    p.host.service.state("playing"); p.host.service.snapshot.duration = 180;
    p.tick(); p.eof();
    require(p.host.service.last().action == Action::Play && p.host.service.last().value == 2,
            "rejected unknown-duration seek preserves automatic EOF advancement");
}

void unicodeAndPageSelection() {
    Player p(9);
    p.host.service.titles[4] = std::string(46, 'A') + "\xe2\x82\xac" + "\xf0\x9f\x8e\xb5" + std::string(30, 'Z');
    p.host.service.titles[5] = std::string("bad\xff\xc0\xaf") + std::string(86, 'x');
    p.start(); p.action("library"); p.action("all"); p.action("page_next"); p.action("page_next");
    p.action("pick1"); p.loadQueue(); p.acknowledge();
    require(p.host.service.snapshot.track == 5, "paged track selection chooses absolute queue index");
    p.action("next"); p.acknowledge();
    require(p.runtime.active(), "malformed filename bytes are sanitized before UI");
    p.action("library"); p.action("all");
    for (unsigned i = 0; i < 4; ++i) p.action("page_next");
    require(std::strstr(p.host.view.body, "9-9 of 9") && !p.has("page_next"), "final partial page is bounded");
}

void duplicatePlaybackAcknowledgements() {
    Player p(2); p.host.service.list("Duplicate tracks", {1,1,2}); p.start();
    p.action("library"); p.action("lists"); p.action("pick1"); p.action("pick1");
    p.loadQueue(); p.acknowledge();
    p.action("next"); // Accepted second occurrence of the same track ID.
    p.host.service.finish(); p.tick(); // First occurrence EOF arrives late.
    require(p.host.service.plays() == std::vector<int>({1,1}), "old same-ID EOF cannot acknowledge queued duplicate");
    p.host.service.apply(); p.host.service.finish(); p.tick(); // Short new occurrence: no playing poll.
    require(p.host.service.plays() == std::vector<int>({1,1,2}), "playback sequence acknowledges short duplicate then advances to next row");

    Player rapid(1); rapid.start(); rapid.playAll();
    rapid.options(); rapid.action("repeat"); rapid.main();
    rapid.action("next"); rapid.action("next");
    rapid.host.service.apply(1); rapid.tick(); // The first of two identical queued requests.
    rapid.host.service.finish(); rapid.tick();
    require(rapid.host.service.plays().size() == 3, "intermediate identical Play acknowledgement cannot advance last requested position");
    rapid.host.service.apply(1); rapid.host.service.finish(); rapid.tick();
    require(rapid.host.service.plays().size() == 4, "last identical Play and short EOF advance exactly once");

    Player wrap(2); wrap.host.service.snapshot.playback = UINT32_MAX; wrap.start(); wrap.playAll(); wrap.eof();
    require(wrap.host.service.last().action == Action::Play && wrap.host.service.last().value == 2,
            "playback acknowledgement handles uint32 sequence wrap");
}

void nativeStartupScan() {
    Player p(2); p.host.service.snapshot.generation = 0; p.host.service.state("scanning"); p.start();
    p.action("play"); p.tick(2);
    require(p.host.service.plays().empty(), "play waits while initial native scan is still running");
    ++p.host.service.snapshot.generation; p.host.service.state("idle"); p.tick();
    p.action("play"); p.loadQueue(); p.acknowledge(); p.action("next");
    require(p.host.service.last().action == Action::Play && p.host.service.last().value == 2,
            "initial scan completion yields a consistent queue generation");
}

void sustainedPlayback() {
    Player p(512); p.start(); p.playAll();
    const auto calls = p.heap.calls;
    const auto bytes = p.heap.allocatedBytes;
    const auto shows = p.host.shows;
    // Same snapshot for 30 simulated minutes: catches unnecessary UI churn.
    for (unsigned second = 0; second < 1800; ++second) p.tick(20);
    require(p.host.shows == shows, "unchanged status causes no new UI views during 30 simulated minutes");
    std::printf("Player unchanged-status: %zu allocator calls, %zu bytes allocated, %u views in 30 simulated minutes\n",
                p.heap.calls - calls, p.heap.allocatedBytes - bytes, p.host.shows - shows);
    // Two simulated hours of changing timestamps and natural track changes.
    for (unsigned second = 1; second <= 7200; ++second) {
        p.host.service.snapshot.position = second % 180;
        if (second % 180 == 0) p.host.service.finish();
        p.tick(20);
        p.host.service.apply();
    }
    require(p.runtime.active() && p.host.exits == 0, "2.5-hour simulated playback remains active without exit");
    require(p.heap.peak <= 256 * 1024, "long-running app stays within manifest allocator quota");
    std::printf("Player 2.5-hour simulation: actual allocator peak %zu bytes, live %zu bytes\n", p.heap.peak, p.heap.live);
    p.runtime.stop();
    require(p.heap.live == 0, "long-running app returns every Lua allocation on stop");
}

void rapidVolumeAndEqualizer() {
    Player p; p.start(); p.playAll(); p.volume();
    p.host.service.queueCapacity = 8;
    for (unsigned i = 0; i < 100; ++i) {
        p.host.service.accept = i % 9 != 0;
        p.action("louder");
        require(p.host.exits == 0 && p.runtime.active() && p.host.view.kind == ViewKind::Menu,
                "rapid volume tap stays in live large-button menu");
        if (i % 13 == 12) p.host.service.apply();
        if (i % 21 == 20) p.tick(); // Deliberately stale snapshots between taps.
    }
    p.host.service.accept = true; p.acknowledge();
    require(p.host.service.snapshot.volume == 100, "100 rapid volume taps including queue-busy reach bounded final volume");
    const auto commands = p.host.service.commands.size();
    const auto shows = p.host.shows;
    for (unsigned i = 0; i < 100; ++i) p.action("louder");
    require(p.host.service.commands.size() == commands && p.host.shows == shows,
            "repeated taps at volume maximum send no redundant command or view");
    p.main(); p.action("sound"); p.action("equalizer");
    p.host.service.accept = false; p.action("eq0");
    require(p.has("eq0") && std::strstr(p.message(), "Busy"), "busy EQ command stays on preset page with feedback");
    p.host.service.accept = true; p.action("eq1"); p.acknowledge();
    require(p.host.service.snapshot.equalizer == 1, "voice preset is passed to bounded native Equalizer command");
    p.action("equalizer"); p.action("eq_next"); p.action("eq2"); p.acknowledge();
    require(p.host.service.snapshot.equalizer == 2, "second EQ page selects warm preset");
    p.action("equalizer"); p.action("eq_next"); p.action("eq3"); p.acknowledge();
    require(p.host.service.snapshot.equalizer == 3, "small speaker preset is reachable with large controls");
    p.action("equalizer"); p.action("eq0"); p.acknowledge();
    require(p.host.service.snapshot.equalizer == 0, "neutral preset resets equalizer through first page");
    p.scanMenu(); p.action("close"); p.action("cancel_close");
    require(p.host.exits == 0 && p.has("close"), "cancelled exit returns to library options without stopping");
    p.main(); p.key("back"); p.key("back");
    require(p.host.exits == 0, "repeated short Back on main never exits the app");
    p.eof(); require(p.host.service.last().action == Action::Play, "volume and EQ interactions preserve EOF queue advancement");
}

void repeatedPlayerLifecycle() {
    Player p(3);
    for (unsigned cycle = 0; cycle < 20; ++cycle) {
        p.start(); p.playAll(); p.volume(); p.action("louder"); p.acknowledge();
        p.runtime.stop(); p.host.service.apply();
        require(!p.runtime.active() && p.heap.live == 0, "repeated player stop releases all Lua allocations");
        require(!std::strcmp(p.host.service.snapshot.state, "stopped"), "normal player shutdown requests audio stop");
    }
}
} // namespace

int main() {
    packagedManifest(); transportAndStaleSnapshots(); playlistOrderAndRepeat();
    shuffleAndLargeQueue(); controlsAndBusy(); errorsAndScanGeneration(); seekWaitsForDuration(); unicodeAndPageSelection();
    duplicatePlaybackAcknowledgements(); nativeStartupScan(); rapidVolumeAndEqualizer(); repeatedPlayerLifecycle(); sustainedPlayback();
    std::printf("MP3 Lua player: %d checks passed; maximum sampled VM heap %zu bytes\n", checks, maxVmBytes);
    return 0;
}
