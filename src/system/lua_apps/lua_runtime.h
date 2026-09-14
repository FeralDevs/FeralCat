#pragma once

#include <cstddef>
#include <cstdint>

struct lua_State;
struct lua_Debug;
namespace meow::media { class Host; }

namespace meow { namespace luaapps {

struct Item {
    char id[32] = {};
    char label[64] = {};
};

enum class ViewKind { List, Player, Menu };
struct PlayerView {
    char title[96] = {}, subtitle[96] = {}, status[96] = {};
    std::uint32_t position = 0, duration = 0;
    std::uint8_t volume = 35;
    bool playing = false;
};

struct View {
    char title[64] = {};
    char body[1024] = {};
    Item items[8] = {};
    std::size_t itemCount = 0;
    bool compact = false;
    ViewKind kind = ViewKind::List;
    PlayerView player;
};

// Implementations must not throw or enter Lua. show() copies the bounded view;
// requestExit() queues a request for the owner, rather than deleting the VM.
struct Host {
    virtual ~Host() = default;
    virtual void show(const View& view) = 0;
    virtual std::uint32_t uptimeMs() = 0;
    virtual void requestExit() = 0;
    virtual meow::media::Host* audio() { return nullptr; }
    virtual void captureBack(bool) {}
};

struct Limits {
    std::size_t memoryBytes = 256u * 1024u;
    std::uint32_t instructionBudget = 50000;
    std::uint32_t callbackMs = 20;
    std::uint32_t startupMs = 250; // Compilation/initialization, outside audio servicing.
    std::size_t sourceBytes = 64u * 1024u;
};

// A failed resize must leave the old block valid. A zero newSize frees it.
// This allocator covers the Lua heap, not the separately bounded native View,
// source buffer, or resources owned by Host.
struct Allocator {
    void* context = nullptr;
    void* (*resize)(void* context, void* pointer,
                    std::size_t oldSize, std::size_t newSize) = nullptr;
};

class Runtime {
public:
    explicit Runtime(Host& host, Limits limits = {}, Allocator allocator = {});
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    bool start(const char* source, std::size_t length,
               char* error = nullptr, std::size_t errorCapacity = 0);
    bool event(const char* type, const char* value);
    bool tick(std::uint32_t elapsedMs);
    void stop();
    bool active() const { return state_ != nullptr; }
    const char* error() const { return error_; }
    std::size_t memoryUsed() const { return memoryUsed_; }
    // Heap still owned at the last fault, captured before release() frees it.
    // Zero after a new start or if failure occurred before creating a VM.
    std::size_t lastFaultMemoryBytes() const { return lastFaultMemoryBytes_; }

private:
    enum class Call { Setup, Start, Event, Tick, Stop };
    Host& host_;
    Limits limits_;
    Allocator allocator_;
    lua_State* state_ = nullptr;
    std::size_t memoryUsed_ = 0;
    std::size_t lastFaultMemoryBytes_ = 0;
    char error_[192] = {};
    bool insideCall_ = false;
    bool exitRequested_ = false;
    bool exhausted_ = false;
    Call call_ = Call::Setup;
    const char* source_ = nullptr;
    std::size_t sourceLength_ = 0;
    char eventType_[32] = {};
    char eventValue_[64] = {};
    std::uint32_t elapsedMs_ = 0;
    std::uint32_t startedMs_ = 0;
    std::uint32_t callBudgetMs_ = 20;
    std::uint32_t remainingInstructions_ = 0;
    int hookInterval_ = 100;

    bool invoke(Call call);
    void release();
    void fail(const char* message);
    static Runtime& owner(lua_State* state);
    static void* allocate(void*, void*, std::size_t, std::size_t);
    static int dispatch(lua_State*);
    static void instructionHook(lua_State*, lua_Debug*);
    static int show(lua_State*);
    static int menu(lua_State*);
    static int player(lua_State*);
    static int showView(lua_State*, ViewKind);
    static int exit(lua_State*);
    static int uptime(lua_State*);
    static int has(lua_State*);
    static int captureBack(lua_State*);
    static int audioCommand(lua_State*);
    static int audioStatus(lua_State*);
    static int audioTracks(lua_State*);
    static int audioPlaylists(lua_State*);
    static void setup(lua_State*);
    static void callback(lua_State*, const char* name, int arguments);
};

}} // namespace meow::luaapps
