#include "lua_runtime.h"
#include "app_manifest.h"
#include "../../src/system/media/media_api.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

using namespace meow::luaapps;

namespace {
int checks = 0;
void require(bool condition, const char* message)
{
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

struct TestHost : Host {
    View view;
    unsigned shows = 0;
    unsigned exits = 0;
    std::uint32_t clock = 100;
    std::uint32_t clockStep = 0;
    void show(const View& value) override { view = value; ++shows; }
    std::uint32_t uptimeMs() override { const auto result = clock; clock += clockStep; return result; }
    void requestExit() override { ++exits; }
};

bool start(Runtime& runtime, const char* source)
{
    return runtime.start(source, std::strlen(source));
}

void expectFault(const char* source, const char* description, Limits limits = {})
{
    TestHost host;
    Runtime runtime(host, limits);
    require(!start(runtime, source), description);
    require(!runtime.active(), "fault closes state");
    require(runtime.memoryUsed() == 0, "fault releases Lua heap");
    require(runtime.error()[0] != '\0', "fault has diagnostic");
}

struct TrackingAllocator {
    std::size_t live = 0;
    std::size_t calls = 0;
    std::size_t failAfter = static_cast<std::size_t>(-1);
    static void* resize(void* context, void* pointer, std::size_t oldSize, std::size_t newSize)
    {
        auto& allocator = *static_cast<TrackingAllocator*>(context);
        if (newSize == 0) {
            std::free(pointer);
            allocator.live -= oldSize;
            return nullptr;
        }
        if (allocator.calls++ >= allocator.failAfter && newSize > oldSize) return nullptr;
        void* result = std::realloc(pointer, newSize);
        if (result) allocator.live = allocator.live - oldSize + newSize;
        return result;
    }
};

void lifecycle()
{
    TestHost host;
    Runtime runtime(host);
    const char* source = R"lua(
        count = 0
        function on_start()
          assert(meow.api_version == 1)
          assert(meow.system.has('ui') and meow.system.has('system'))
          assert(not meow.system.has('audio') and not meow.system.has('network'))
          assert(meow.system.uptime_ms() == 100)
          meow.ui.show('Counter', 'ready', {{id='inc',label='Increment'}})
        end
        function on_event(kind, value)
          assert(kind == 'action' and value == 'inc')
          count = count + 1
          meow.ui.show('Counter', tostring(count), {{id='inc',label='Increment'}})
        end
        function on_tick(dt) assert(dt == 50) end
        function on_stop() meow.ui.show('Stopped', tostring(count), {}) end
    )lua";
    require(start(runtime, source), runtime.error());
    require(runtime.active() && runtime.memoryUsed() > 0, "started state owns memory");
    require(host.shows == 1 && !std::strcmp(host.view.body, "ready"), "on_start renders");
    require(host.view.itemCount == 1 && !std::strcmp(host.view.items[0].id, "inc"), "items copied");
    require(runtime.event("action", "inc"), runtime.error());
    require(!std::strcmp(host.view.body, "1"), "event updates persistent state");
    require(runtime.tick(50), runtime.error());
    runtime.stop();
    require(!runtime.active() && runtime.memoryUsed() == 0, "stop releases state");
    require(!std::strcmp(host.view.title, "Stopped"), "on_stop called");
    const unsigned shows = host.shows;
    runtime.stop();
    require(host.shows == shows, "stop is idempotent");
    require(!runtime.tick(50) && !runtime.event("key", "A"), "inactive state rejects calls");
}

void sandbox()
{
    TestHost host;
    Runtime runtime(host);
    require(start(runtime, R"lua(
      assert(io == nil and os == nil and package == nil and debug == nil)
      assert(require == nil and load == nil and loadfile == nil and dofile == nil)
      assert(pcall == nil and xpcall == nil and coroutine == nil)
      assert(setmetatable == nil and getmetatable == nil and collectgarbage == nil)
      assert(print == nil and warn == nil)
      assert(string.dump == nil and string.find == nil and string.match == nil)
      assert(string.gsub == nil and string.gmatch == nil and string.format == nil and string.rep == nil)
      assert(table.move == nil)
      assert(('x').dump == nil and ('x').gsub == nil)
      assert(math.floor(2.8) == 2 and utf8.len('hello') == 5)
      assert(string.upper('abc') == 'ABC')
      local values = {3, 1, 2}; table.sort(values)
      assert(table.concat(values, ',') == '1,2,3')
      assert(meow.system.has('ui\0extra') == false)
    )lua"), runtime.error());
    runtime.stop();
    expectFault("load('return 1')", "dynamic source loader unavailable");
    expectFault("pcall(function() while true do end end)", "script cannot catch budget failures");
    expectFault("setmetatable({}, {__gc=function() while true do end end})", "script cannot install shutdown finalizer");
}

void errorsAndBudgets()
{
    expectFault("function on_start( end", "syntax error");
    expectFault("error('broken')", "top-level error");
    expectFault("function on_start() error({}) end", "non-string errors handled safely");
    expectFault("on_start = 7", "invalid callback type");
    expectFault("while true do end", "one-line loop interrupted");
    expectFault("function on_start() while true do end end", "callback loop interrupted");
    expectFault("local function recurse() return recurse() end; recurse()", "tail-recursive loop interrupted");
    expectFault("table.sort({3,2,1}, function() while true do end end)", "Lua comparator remains interruptible");
    {
        TestHost parserHost;
        Runtime parserRuntime(parserHost);
        const std::string deeplyNested = "local value = " + std::string(128, '(') + "1" + std::string(128, ')');
        require(!parserRuntime.start(deeplyNested.data(), deeplyNested.size()), "deep parser nesting rejected");
        require(std::strstr(parserRuntime.error(), "C stack overflow") != nullptr,
                "parser rejected at configured C nesting limit");
        require(!parserRuntime.active() && parserRuntime.memoryUsed() == 0, "parser nesting fault cleanup");
    }
    Limits small;
    small.memoryBytes = 64 * 1024;
    small.instructionBudget = 1000000;
    expectFault("local t = {}; for i=1,100000 do t[i] = {i,i,i,i} end", "heap quota", small);
    small.memoryBytes = 512;
    expectFault("return", "initial VM OOM is recoverable", small);

    TestHost host;
    Runtime runtime(host);
    require(start(runtime, "function on_tick() while true do end end"), runtime.error());
    require(!runtime.tick(50) && !runtime.active(), "tick loop interrupted");
    require(runtime.memoryUsed() == 0, "tick fault cleanup");
    require(start(runtime, "function on_event() error('event failed') end"), runtime.error());
    require(!runtime.event("key", "A"), "event error caught");
    require(!runtime.active() && runtime.memoryUsed() == 0, "event fault cleanup");
    require(start(runtime, "function on_stop() while true do end end"), runtime.error());
    runtime.stop();
    require(!runtime.active() && runtime.memoryUsed() == 0 && runtime.error()[0], "bounded on_stop");

    require(start(runtime, "function on_tick() for i=1,10000 do local x=i*i end end"), runtime.error());
    host.clockStep = 5;
    require(!runtime.tick(50), "host-clock timeout");
    require(!runtime.active() && runtime.memoryUsed() == 0, "timeout cleanup");
    TestHost startupHost;
    startupHost.clockStep = 100;
    Runtime startupRuntime(startupHost);
    require(start(startupRuntime, "return"), "startup has compilation allowance beyond callback budget");
    require(!startupRuntime.tick(50), "normal callbacks retain short deadline after startup");
    startupHost.clockStep = 300;
    require(!start(startupRuntime, "return") && startupRuntime.memoryUsed() == 0,
            "startup deadline remains bounded and releases heap");
}

void viewsAndInput()
{
    expectFault("meow.ui.show('title','body',{{id='same',label='A'},{id='same',label='B'}})", "duplicate IDs");
    expectFault("meow.ui.show('title','body',{{id='../path',label='A'}})", "invalid ID");
    expectFault("meow.ui.show('title','body',{[2]={id='x',label='A'}})", "sparse items");
    expectFault("meow.ui.show('title','body',{hidden={id='x',label='A'}})", "non-array items");
    expectFault("local a={}; for i=1,9 do a[i]={id=tostring(i),label='A'} end; meow.ui.show('T','B',a)", "item count bound");
    expectFault("meow.ui.show(1,'body',{})", "strict title type");
    expectFault("meow.ui.show('x\\0','body',{})", "NUL title rejected");
    const std::string oversized = "meow.ui.show('" + std::string(64, 'x') + "','body',{})";
    expectFault(oversized.c_str(), "title byte bound");
    const std::string largeBody = "meow.ui.show('T','" + std::string(1024, 'x') + "',{})";
    expectFault(largeBody.c_str(), "body byte bound");

    TestHost host;
    Runtime runtime(host);
    char error[8] = {};
    const char binary[] = {'\x1b', 'L', 'u', 'a', 'x'};
    require(!runtime.start(binary, sizeof(binary), error, sizeof(error)), "bytecode rejected");
    require(error[sizeof(error)-1] == '\0' && runtime.memoryUsed() == 0, "bounded diagnostic");
    const char nul[] = {'r','e','t','u','r','n','\0','x'};
    require(!runtime.start(nul, sizeof(nul)), "embedded source NUL rejected");
    const std::string tooLarge(64 * 1024 + 1, ' ');
    require(!runtime.start(tooLarge.data(), tooLarge.size()), "source size bound");
    require(start(runtime, "return"), runtime.error());
    require(!runtime.event("not-an-event", "x") && !runtime.active(), "event kind bound");
}

void nativeViews()
{
    TestHost host;
    Runtime runtime(host);
    require(start(runtime, "meow.ui.player({title='Song'})"), runtime.error());
    require(host.shows == 1 && host.view.kind == ViewKind::Player, "player uses native Player view");
    require(!std::strcmp(host.view.title, "MP3 Player") && !std::strcmp(host.view.player.title, "Song"),
            "player screen title and track title are independent copied strings");
    require(host.view.player.position == 0 && host.view.player.duration == 0 &&
            host.view.player.volume == 35 && !host.view.player.playing,
            "omitted player fields use bounded defaults");
    require(host.view.itemCount == 5 && !std::strcmp(host.view.items[0].id, "previous") &&
            !std::strcmp(host.view.items[4].id, "sound"), "player has fixed transport/library/sound actions");
    require(start(runtime, R"lua(
        local view = {title='Original',subtitle='Artist',status='Playing',playing=true,
                      position=4294967295,duration=4294967295,volume=100}
        meow.ui.player(view)
        view.title='Mutated'; view.subtitle='Changed'; view.volume=0
        function on_event()
          meow.ui.menu('Library','Choose a track',{{id='back',label='Back'}},true)
        end
    )lua"), runtime.error());
    require(!std::strcmp(host.view.player.title, "Original") && !std::strcmp(host.view.player.subtitle, "Artist") &&
            !std::strcmp(host.view.player.status, "Playing"), "player copies metadata before Lua mutates its table");
    require(host.view.player.position == UINT32_MAX && host.view.player.duration == UINT32_MAX &&
            host.view.player.volume == 100 && host.view.player.playing, "player preserves exact unsigned time and volume boundaries");
    require(runtime.event("action", "library"), runtime.error());
    require(host.view.kind == ViewKind::Menu && host.view.itemCount == 1 && host.view.compact,
            "menu uses bounded native menu layout with optional compact flag");
    require(host.view.player.title[0] == 0 && host.view.player.position == 0,
            "changing view kinds does not retain prior player metadata");
    const std::string playerLimit = "meow.ui.player({title='" + std::string(95, 't') +
        "',subtitle='" + std::string(95, 's') + "',status='" + std::string(95, 'x') +
        "',position=0,duration=0,volume=0,playing=false})";
    require(start(runtime, playerLimit.c_str()), runtime.error());
    require(std::strlen(host.view.player.title) == 95 && std::strlen(host.view.player.subtitle) == 95 &&
            std::strlen(host.view.player.status) == 95 && host.view.player.volume == 0,
            "player accepts exact string capacities and lower numeric bounds");
    const std::string menuLimit = "meow.ui.menu('" + std::string(63, 't') + "','" + std::string(127, 'b') +
        "',{{id='" + std::string(31, 'a') + "',label='" + std::string(63, 'l') +
        "'},{id='b',label='B'},{id='c',label='C'},{id='d',label='D'}})";
    require(start(runtime, menuLimit.c_str()), runtime.error());
    require(host.view.kind == ViewKind::Menu && host.view.itemCount == 4 &&
            std::strlen(host.view.title) == 63 && std::strlen(host.view.body) == 127 &&
            std::strlen(host.view.items[0].id) == 31 && std::strlen(host.view.items[0].label) == 63,
            "menu accepts exact title/body/item limits");
    require(start(runtime, "meow.ui.menu('Empty','',{})"), runtime.error());
    require(host.view.kind == ViewKind::Menu && host.view.itemCount == 0, "empty menu is valid");

    auto invalidView = [](const std::string& source) {
        TestHost badHost;
        Runtime bad(badHost);
        require(!start(bad, source.c_str()), "invalid player/menu input is rejected");
        require(!bad.active() && bad.memoryUsed() == 0 && bad.lastFaultMemoryBytes() > 0,
                "view validation fault retains heap diagnostic and frees VM");
        require(badHost.shows == 0 && badHost.exits == 0,
                "invalid view never reaches native UI or requests exit");
    };
    for (const char* source : {
        "meow.ui.player()", "meow.ui.player('Song')", "meow.ui.player({}, {})",
        "meow.ui.player({})", "meow.ui.player({title=1})", "meow.ui.player({title=false})",
        "meow.ui.player({title='x',subtitle=1})", "meow.ui.player({title='x',status=false})",
        "meow.ui.player({title='x'..string.char(0)})",
        "meow.ui.player({title='x',subtitle=string.char(0)})",
        "meow.ui.player({title='x',status=string.char(0)})",
        "meow.ui.player({title='x',position=-1})", "meow.ui.player({title='x',position=4294967296})",
        "meow.ui.player({title='x',duration=-1})", "meow.ui.player({title='x',duration=4294967296})",
        "meow.ui.player({title='x',position=1.5})", "meow.ui.player({title='x',duration='12'})",
        "meow.ui.player({title='x',volume=-1})", "meow.ui.player({title='x',volume=101})",
        "meow.ui.player({title='x',volume=true})", "meow.ui.player({title='x',playing=1})",
        "meow.ui.menu()", "meow.ui.menu('T','B',{},false,true)",
        "meow.ui.menu(1,'B',{})", "meow.ui.menu('T',false,{})",
        "meow.ui.menu('T','B',{},1)", "meow.ui.menu('T','B',false)",
        "meow.ui.menu('T','B',{{id='x',label='A'},{id='x',label='B'}})",
        "meow.ui.menu('T','B',{{id='../x',label='A'}})",
        "meow.ui.menu('T','B',{[2]={id='x',label='A'}})",
        "meow.ui.menu('T','B',{hidden={id='x',label='A'}})",
        "meow.ui.menu('T','B',{{id='x',label='A'},hidden={id='y',label='B'}})",
        "meow.ui.menu('T','B',{3})", "meow.ui.menu('T','B',{{id='',label='A'}})",
        "meow.ui.menu('T','B',{{id='x',label=1}})",
        "meow.ui.menu('T','B',{{id=string.char(0),label='A'}})",
        "local a={};for i=1,5 do a[i]={id=tostring(i),label='A'} end;meow.ui.menu('T','B',a)"
    }) {
        invalidView(source);
    }
    for (const char* field : {"title", "subtitle", "status"})
        invalidView(std::string("meow.ui.player({title='x',") + field + "='" + std::string(96, 'x') + "'})");
    invalidView("meow.ui.menu('" + std::string(64, 'x') + "','',{})");
    invalidView("meow.ui.menu('T','" + std::string(128, 'x') + "',{})");
    invalidView("meow.ui.menu('T','B',{{id='" + std::string(32, 'x') + "',label='A'}})");
    invalidView("meow.ui.menu('T','B',{{id='x',label='" + std::string(64, 'x') + "'}})");
    TestHost exitHost;
    Runtime exitRuntime(exitHost);
    require(start(exitRuntime, "meow.app.exit();meow.ui.player({title='ignored'});meow.ui.menu('T','B',{})"), exitRuntime.error());
    require(exitHost.exits == 1 && exitHost.shows == 0, "player/menu rendering is suppressed after an explicit exit");
}

void faultMemoryDiagnostics()
{
    TestHost host;
    Runtime runtime(host);
    require(runtime.lastFaultMemoryBytes() == 0, "fresh runtime has no previous heap fault");
    require(start(runtime, "keep={};for i=1,40 do keep[i]={i,i,i} end;function on_event() error('diagnostic') end"), runtime.error());
    const auto liveBytes = runtime.memoryUsed();
    require(!runtime.event("action", "fault"), "event fault is reported");
    const auto faultBytes = runtime.lastFaultMemoryBytes();
    require(liveBytes > 0 && faultBytes > 0 && faultBytes <= Limits{}.memoryBytes && runtime.memoryUsed() == 0,
            "fault stores pre-release heap while allocator reports complete cleanup");
    runtime.stop();
    require(runtime.lastFaultMemoryBytes() == faultBytes, "idempotent cleanup preserves last fault diagnostic");
    require(start(runtime, "return"), runtime.error());
    require(runtime.lastFaultMemoryBytes() == 0, "new start clears old fault diagnostic");
    runtime.stop();
    require(runtime.memoryUsed() == 0 && runtime.lastFaultMemoryBytes() == 0, "normal stop does not invent a fault");
    require(!runtime.start(nullptr, 0) && runtime.lastFaultMemoryBytes() == 0,
            "rejected source before VM allocation reports zero fault heap");
    require(start(runtime, "return"), runtime.error());
    require(!runtime.event("invalid", "x") && runtime.lastFaultMemoryBytes() > 0 && runtime.memoryUsed() == 0,
            "native event validation also captures heap before release");
}

void exitAndIsolation()
{
    TestHost host;
    Runtime runtime(host);
    require(start(runtime, "function on_event() meow.app.exit(); while true do end end"), runtime.error());
    require(runtime.event("key", "B"), runtime.error());
    require(host.exits == 1 && runtime.active() && runtime.error()[0] == '\0', "exit is queued and graceful");
    require(!runtime.tick(50), "exit suppresses further dispatch");
    runtime.stop();
    require(runtime.memoryUsed() == 0, "owner completes queued exit");
    for (int i = 0; i < 100; ++i) {
        require(start(runtime, "assert(old_app == nil); old_app='private'"), runtime.error());
        runtime.stop();
        require(runtime.memoryUsed() == 0, "repeated app close reclaims heap");
    }
    require(start(runtime, "function on_stop() meow.ui.show('Destructor','done',{}) end"), runtime.error());
    runtime.stop();
    require(!std::strcmp(host.view.title, "Destructor"), "final stop callback");
    {
        Runtime scoped(host);
        require(start(scoped, "function on_stop() meow.ui.show('RAII','done',{}) end"), scoped.error());
    }
    require(!std::strcmp(host.view.title, "RAII"), "destructor stops live VM");
}

void allocationFailures()
{
    const char* source = "function on_start() meow.ui.show('T','B',{}) end";
    TestHost baselineHost;
    TrackingAllocator baseline;
    {
        Runtime runtime(baselineHost, {}, {&baseline, TrackingAllocator::resize});
        require(start(runtime, source), runtime.error());
    }
    require(baseline.live == 0, "baseline custom allocator cleanup");
    // Deny allocation at each of the early setup stages; lua_newstate and
    // library/API construction must unwind without panic or leaked blocks.
    for (std::size_t failAfter = 0; failAfter <= baseline.calls; ++failAfter) {
        TestHost host;
        TrackingAllocator tracking;
        tracking.failAfter = failAfter;
        {
            Runtime runtime(host, {}, {&tracking, TrackingAllocator::resize});
            start(runtime, source);
            runtime.stop();
            require(runtime.memoryUsed() == 0, "injected allocator cleanup counter");
        }
        require(tracking.live == 0, "injected allocator actually released blocks");
    }
    TestHost host;
    TrackingAllocator tracking;
    Runtime runtime(host, {}, {&tracking, TrackingAllocator::resize});
    require(start(runtime, "function on_event(kind,value) assert(type(value)=='string') end"), runtime.error());
    tracking.failAfter = tracking.calls;
    require(!runtime.event("action", "a_new_event_string_not_in_the_script"), "event argument OOM is protected");
    require(!runtime.active() && runtime.memoryUsed() == 0 && tracking.live == 0, "event argument OOM cleanup");
}

std::string readFixture(const char* relativePath, std::size_t maximum)
{
    const std::string path = std::string(MEOW_TEST_ROOT) + "/" + relativePath;
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(input.good(), "example package fixture exists");
    const auto length = input.tellg();
    require(length > 0 && static_cast<std::size_t>(length) <= maximum, "fixture size bounded");
    std::string data(static_cast<std::size_t>(length), '\0');
    input.seekg(0);
    require(static_cast<bool>(input.read(&data[0], static_cast<std::streamsize>(data.size()))),
            "example package fixture read completely");
    require(input.peek() == std::char_traits<char>::eof(), "fixture did not grow while reading");
    return data;
}

void examplePackage()
{
    const auto manifestText = readFixture("sd files/apps/hello_meow/manifest.ini", kManifestMaxBytes);
    const auto source = readFixture("sd files/apps/hello_meow/main.lua", kScriptMaxBytes);
    Manifest manifest = {};
    char error[192] = {};
    require(parseManifest(manifestText.data(), manifestText.size(), manifest, error, sizeof(error)), error);
    require(!std::strcmp(manifest.id, "hello_meow") && !std::strcmp(manifest.entry, "main.lua"),
            "example metadata matches package files");
    require(manifest.memoryBytes == 128 * 1024 && manifest.api == 1, "example API and heap budget");
    Limits limits;
    limits.memoryBytes = manifest.memoryBytes;
    TestHost host;
    Runtime runtime(host, limits);
    require(runtime.start(source.data(), source.size()), runtime.error());
    require(!std::strcmp(host.view.title, "Hello Meow") && std::strstr(host.view.body, "Count: 0"),
            "real SD example starts with visible zero");
    require(host.view.itemCount == 3 && !std::strcmp(host.view.items[0].id, "increment"), "real SD example actions");
    require(!host.view.compact, "existing example retains list layout");
    require(runtime.event("action", "increment"), runtime.error());
    require(std::strstr(host.view.body, "Count: 1") != nullptr, "real SD increment");
    require(runtime.event("action", "reset"), runtime.error());
    require(std::strstr(host.view.body, "Count: 0") != nullptr, "real SD reset");
    require(runtime.event("key", "right"), runtime.error());
    require(std::strstr(host.view.body, "Count: 1") != nullptr, "real SD right key");
    require(runtime.tick(50), runtime.error());
    require(runtime.event("action", "close"), runtime.error());
    require(host.exits == 1 && runtime.active(), "real SD close queues owner shutdown");
    runtime.stop();
    require(runtime.error()[0] == '\0' && runtime.memoryUsed() == 0, "real SD stop releases heap");
}

struct FakeMedia : meow::media::Host {
    meow::media::Command last{};
    unsigned commands = 0;
    bool busy = false;
    bool oversized = false;
    uint8_t equalizer = 0;
    bool command(meow::media::Command value) override { last = value; ++commands; return !busy; }
    bool status(meow::media::Status& out) override {
        if (busy) return false;
        std::strcpy(out.state, "playing");
        std::strcpy(out.title, "Example Song");
        out.track = 7; out.tracks = 12; out.playlists = 2;
        out.position = 13; out.duration = 180; out.finished = 4; out.generation = 9; out.playback = 12;
        out.equalizer = equalizer;
        return true;
    }
    bool tracks(uint16_t playlist, size_t offset, size_t limit, meow::media::Page& out) override {
        if (busy) return false;
        require(playlist <= 16 && offset <= 512 && limit >= 1 && limit <= 8, "bounded page reaches native host");
        out.total = 12; out.count = oversized ? 9 : 1;
        out.entries[0].id = 7; std::strcpy(out.entries[0].title, "Example Song");
        return true;
    }
    bool playlists(size_t offset, size_t limit, meow::media::Page& out) override {
        return tracks(0, offset, limit, out);
    }
};
struct AudioTestHost : TestHost {
    FakeMedia media;
    bool allowed = true, back = false;
    meow::media::Host* audio() override { return allowed ? &media : nullptr; }
    void captureBack(bool enabled) override { back = enabled; }
};

void mediaBindings() {
    AudioTestHost host;
    Runtime runtime(host);
    require(start(runtime, R"lua(
        assert(meow.system.has('audio') and not meow.system.has('network'))
        meow.app.capture_back(true)
        meow.ui.show('Player','Time',{{id='play',label='Play'}},true)
        local s = meow.audio.status()
        assert(s.state == 'playing' and s.title == 'Example Song' and s.track == 7)
        assert(s.position == 13 and s.duration == 180 and s.finished == 4 and s.generation == 9)
        assert(s.playback == 12)
        local p = meow.audio.tracks()
        assert(p.total == 12 and #p.items == 1 and p.items[1].id == 7)
        assert(meow.audio.playlists(0, 8).items[1].title == 'Example Song')
        assert(meow.audio.command('volume', 100))
        function on_event(kind, value)
          if value == 'busy' then
            assert(meow.audio.status() == nil and meow.audio.tracks() == nil)
            assert(meow.audio.playlists() == nil and not meow.audio.command('pause'))
          elseif value == 'denied' then meow.audio.command('play', 1)
          end
        end
    )lua"), runtime.error());
    require(host.back && host.media.commands == 1 && host.media.last.value == 100,
            "back capture and bounded command forwarded");
    require(host.view.compact, "compact view reaches native host");
    host.media.busy = true;
    require(runtime.event("action", "busy"), runtime.error());
    host.allowed = false;
    require(!runtime.event("action", "denied") && !runtime.active(), "revoked host denies retained audio API");
    require(runtime.memoryUsed() == 0, "permission fault frees heap");
    for (const char* source : {
        "meow.audio.command('play', 0)", "meow.audio.command('play', 513)",
        "meow.audio.command('volume', 101)", "meow.audio.command('seek', -1)",
        "meow.audio.command('seek', 65536)", "meow.audio.command('play', '/mp3/a.mp3')",
        "meow.audio.command('volume')", "meow.audio.command('network', 'https://example.com')",
        "meow.audio.tracks(17)", "meow.audio.tracks(0, 513)",
        "meow.audio.tracks(0, 0, 9)", "meow.audio.tracks(0, 0, 0)",
        "meow.audio.playlists(-1)", "meow.app.capture_back('yes')"
    }) {
        AudioTestHost invalidHost;
        Runtime invalid(invalidHost);
        require(!start(invalid, source), "invalid media argument rejected");
        require(invalidHost.media.commands == 0 && invalid.memoryUsed() == 0,
                "argument failure never commands hardware and frees heap");
    }
    AudioTestHost oversizedHost;
    oversizedHost.media.oversized = true;
    Runtime oversized(oversizedHost);
    require(!start(oversized, "meow.audio.tracks()"), "oversized native page rejected");
    require(oversized.memoryUsed() == 0, "native contract fault closes Lua");
}

void equalizerBindings()
{
    for (int value = 0; value <= 3; ++value) {
        AudioTestHost host;
        host.media.equalizer = static_cast<uint8_t>(value);
        Runtime runtime(host);
        const std::string source = "assert(meow.audio.command('eq'," + std::to_string(value) +
            "));assert(meow.audio.status().equalizer==" + std::to_string(value) + ")";
        require(start(runtime, source.c_str()), runtime.error());
        require(host.media.commands == 1 && host.media.last.action == meow::media::Action::Equalizer &&
                host.media.last.value == value && host.exits == 0,
                "each EQ preset forwards exact native value and remains in the app");
    }
    for (const char* source : {
        "meow.audio.command('eq',-1)", "meow.audio.command('eq',4)",
        "meow.audio.command('eq',1.5)", "meow.audio.command('eq','2')",
        "meow.audio.command('eq')", "meow.audio.command('eq',nil)",
        "meow.audio.command('eq',{})", "meow.audio.command('eq',true)",
        "meow.audio.command('eq',1,2)"
    }) {
        AudioTestHost host;
        Runtime runtime(host);
        require(!start(runtime, source), "invalid EQ input rejected");
        require(host.media.commands == 0 && host.exits == 0 && runtime.memoryUsed() == 0,
                "invalid EQ never reaches audio or exits and releases the VM");
    }
    AudioTestHost busyHost;
    busyHost.media.busy = true;
    Runtime busy(busyHost);
    require(start(busy, "assert(not meow.audio.command('eq',3));assert(meow.audio.status()==nil)"), busy.error());
    require(busy.active() && busyHost.exits == 0 && busyHost.media.commands == 1,
            "full native queue returns false without faulting or exiting Lua");
    AudioTestHost deniedHost;
    deniedHost.allowed = false;
    Runtime denied(deniedHost);
    require(start(denied, "assert(meow.audio==nil and not meow.system.has('audio'))"), denied.error());
    require(deniedHost.media.commands == 0, "EQ does not add audio access without capability");
}
} // namespace

int main()
{
    lifecycle();
    sandbox();
    errorsAndBudgets();
    viewsAndInput();
    nativeViews();
    faultMemoryDiagnostics();
    exitAndIsolation();
    allocationFailures();
    examplePackage();
    mediaBindings();
    equalizerBindings();
    std::printf("Lua runtime: %d checks passed\n", checks);
    return 0;
}
