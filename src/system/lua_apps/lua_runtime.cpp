#include "lua_runtime.h"
#include "../media/media_api.h"

#include <climits>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace meow { namespace luaapps {
namespace {
void copyText(char* out, std::size_t capacity, const char* value)
{
    if (!out || !capacity) return;
    if (!value) value = "Lua error";
    std::size_t count = std::strlen(value);
    if (count >= capacity) count = capacity - 1;
    std::memcpy(out, value, count);
    out[count] = '\0';
}

bool copyEvent(char* out, std::size_t capacity, const char* value)
{
    if (!value) return false;
    const std::size_t count = std::strlen(value);
    if (count >= capacity) return false;
    std::memcpy(out, value, count + 1);
    return true;
}

void checkedText(lua_State* state, int index, char* out,
                 std::size_t capacity, const char* field)
{
    if (lua_type(state, index) != LUA_TSTRING)
        luaL_error(state, "%s must be a string", field);
    std::size_t count = 0;
    const char* value = lua_tolstring(state, index, &count);
    if (count >= capacity || std::memchr(value, '\0', count))
        luaL_error(state, "%s is too long or contains a NUL", field);
    std::memcpy(out, value, count);
    out[count] = '\0';
}

void removeField(lua_State* state, const char* name)
{
    lua_pushnil(state);
    lua_setfield(state, -2, name);
}

void functionField(lua_State* state, const char* name, lua_CFunction function)
{
    lua_pushcfunction(state, function);
    lua_setfield(state, -2, name);
}
} // namespace

Runtime::Runtime(Host& host, Limits limits, Allocator allocator)
    : host_(host), limits_(limits), allocator_(allocator) {}

Runtime::~Runtime() { stop(); }

Runtime& Runtime::owner(lua_State* state)
{
    return **static_cast<Runtime**>(lua_getextraspace(state));
}

void* Runtime::allocate(void* context, void* pointer,
                        std::size_t oldSize, std::size_t newSize)
{
    Runtime& runtime = *static_cast<Runtime*>(context);
    // For a new allocation Lua passes a type tag in oldSize, not bytes.
    if (!pointer) oldSize = 0;
    if (oldSize > runtime.memoryUsed_) return nullptr;
    const std::size_t retained = runtime.memoryUsed_ - oldSize;
    if (newSize > runtime.limits_.memoryBytes - retained) return nullptr;
    void* result = nullptr;
    if (runtime.allocator_.resize) {
        result = runtime.allocator_.resize(runtime.allocator_.context,
                                            pointer, oldSize, newSize);
    } else if (newSize == 0) {
        std::free(pointer);
    } else {
        result = std::realloc(pointer, newSize);
    }
    if (newSize == 0 || result) runtime.memoryUsed_ = retained + newSize;
    return result;
}

void Runtime::fail(const char* message)
{
    lastFaultMemoryBytes_ = memoryUsed_;
    copyText(error_, sizeof(error_), message);
}

void Runtime::release()
{
    if (state_) {
        lua_State* closing = state_;
        state_ = nullptr;
        // Scripts cannot create __gc/__close metamethods: metatable setters,
        // debug, io, userdata constructors and arbitrary native modules are absent.
        lua_sethook(closing, nullptr, 0, 0);
        lua_close(closing);
    }
    source_ = nullptr;
    sourceLength_ = 0;
}

bool Runtime::start(const char* source, std::size_t length,
                    char* outError, std::size_t errorCapacity)
{
    if (insideCall_) return false;
    stop();
    error_[0] = '\0';
    lastFaultMemoryBytes_ = 0;
    exitRequested_ = false;
    exhausted_ = false;
    if (!source || !length || length > limits_.sourceBytes ||
        !limits_.memoryBytes || !limits_.instructionBudget || !limits_.callbackMs || !limits_.startupMs) {
        fail("invalid source size or runtime limits");
    } else if (static_cast<unsigned char>(source[0]) == 0x1b ||
               std::memchr(source, '\0', length)) {
        fail("only Lua text source without NUL bytes is accepted");
    } else {
        state_ = lua_newstate(allocate, this, host_.uptimeMs());
        if (!state_) {
            fail("Lua VM allocation failed");
        } else {
            *static_cast<Runtime**>(lua_getextraspace(state_)) = this;
            source_ = source;
            sourceLength_ = length;
            if (invoke(Call::Setup)) invoke(Call::Start);
            source_ = nullptr;
            sourceLength_ = 0;
        }
    }
    copyText(outError, errorCapacity, error_);
    return state_ != nullptr && error_[0] == '\0';
}

bool Runtime::event(const char* type, const char* value)
{
    if (!state_ || insideCall_ || exitRequested_) return false;
    if (!copyEvent(eventType_, sizeof(eventType_), type) ||
        !copyEvent(eventValue_, sizeof(eventValue_), value) ||
        (std::strcmp(eventType_, "action") && std::strcmp(eventType_, "key"))) {
        fail("invalid event type or value");
        release();
        return false;
    }
    return invoke(Call::Event);
}

bool Runtime::tick(std::uint32_t elapsedMs)
{
    if (!state_ || insideCall_ || exitRequested_) return false;
    elapsedMs_ = elapsedMs;
    return invoke(Call::Tick);
}

void Runtime::stop()
{
    if (!state_ || insideCall_) return;
    // Faults have already closed the state. A normal stop gets one bounded
    // callback, even when exit() was requested during the previous callback.
    exitRequested_ = false;
    invoke(Call::Stop);
    release();
}

bool Runtime::invoke(Call call)
{
    if (!state_ || insideCall_) return false;
    call_ = call;
    callBudgetMs_ = (call == Call::Setup || call == Call::Start) ? limits_.startupMs : limits_.callbackMs;
    exhausted_ = false;
    remainingInstructions_ = limits_.instructionBudget;
    hookInterval_ = limits_.instructionBudget < 100
        ? static_cast<int>(limits_.instructionBudget) : 100;
    startedMs_ = host_.uptimeMs();
    lua_sethook(state_, instructionHook, LUA_MASKCOUNT, hookInterval_);
    insideCall_ = true;
    lua_pushcfunction(state_, dispatch); // A zero-upvalue C function allocates nothing.
    const int result = lua_pcall(state_, 0, 0, 0);
    insideCall_ = false;
    lua_sethook(state_, nullptr, 0, 0);
    const bool timedOut = static_cast<std::uint32_t>(host_.uptimeMs() - startedMs_)
        >= callBudgetMs_;
    if (result != LUA_OK || exhausted_ || timedOut) {
        if (exitRequested_ && !exhausted_ && !timedOut) {
            lua_settop(state_, 0);
            return true;
        }
        if (exhausted_) fail("Lua callback exceeded its instruction or time budget");
        else if (timedOut) fail("Lua callback exceeded its time budget");
        else if (result == LUA_ERRMEM) fail("Lua memory budget or allocation exhausted");
        else if (lua_type(state_, -1) == LUA_TSTRING) fail(lua_tostring(state_, -1));
        else fail("Lua callback failed");
        release();
        return false;
    }
    lua_settop(state_, 0);
    return true;
}

void Runtime::instructionHook(lua_State* state, lua_Debug*)
{
    Runtime& runtime = owner(state);
    if (runtime.exitRequested_) luaL_error(state, "application requested exit");
    const std::uint32_t step = static_cast<std::uint32_t>(runtime.hookInterval_);
    if (runtime.exhausted_ || runtime.remainingInstructions_ <= step ||
        static_cast<std::uint32_t>(runtime.host_.uptimeMs() - runtime.startedMs_)
            >= runtime.callBudgetMs_) {
        runtime.exhausted_ = true;
        luaL_error(state, "Lua callback budget exceeded");
    }
    runtime.remainingInstructions_ -= step;
}

void Runtime::setup(lua_State* state)
{
    const luaL_Reg libraries[] = {
        {LUA_GNAME, luaopen_base}, {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math},
        {LUA_UTF8LIBNAME, luaopen_utf8}, {nullptr, nullptr}
    };
    for (const luaL_Reg* library = libraries; library->name; ++library) {
        luaL_requiref(state, library->name, library->func, 1);
        lua_pop(state, 1);
    }
    lua_pushglobaltable(state);
    const char* blocked[] = {"collectgarbage", "dofile", "load", "loadfile",
        "pcall", "xpcall", "print", "warn", "getmetatable", "setmetatable", nullptr};
    for (const char** name = blocked; *name; ++name) removeField(state, *name);
    lua_pop(state, 1);
    lua_getglobal(state, "string");
    const char* blockedString[] = {"dump", "find", "match", "gmatch", "gsub",
                                   "format", "rep", nullptr};
    for (const char** name = blockedString; *name; ++name) removeField(state, *name);
    lua_pop(state, 1);
    // table.move can iterate an enormous numeric range of nil entries without
    // allocating or executing Lua instructions, so a heap quota cannot bound it.
    lua_getglobal(state, "table");
    removeField(state, "move");
    lua_pop(state, 1);

    lua_newtable(state);
    lua_pushinteger(state, 1);
    lua_setfield(state, -2, "api_version");
    lua_newtable(state);
    functionField(state, "show", show);
    functionField(state, "menu", menu);
    functionField(state, "player", player);
    lua_setfield(state, -2, "ui");
    lua_newtable(state);
    functionField(state, "exit", exit);
    functionField(state, "capture_back", captureBack);
    lua_setfield(state, -2, "app");
    lua_newtable(state);
    functionField(state, "uptime_ms", uptime);
    functionField(state, "has", has);
    lua_setfield(state, -2, "system");
    if (owner(state).host_.audio()) {
        lua_newtable(state);
        functionField(state, "command", audioCommand);
        functionField(state, "status", audioStatus);
        functionField(state, "tracks", audioTracks);
        functionField(state, "playlists", audioPlaylists);
        lua_setfield(state, -2, "audio");
    }
    lua_setglobal(state, "meow");
}

void Runtime::callback(lua_State* state, const char* name, int arguments)
{
    lua_getglobal(state, name);
    if (lua_isnil(state, -1)) {
        lua_pop(state, arguments + 1);
        return;
    }
    if (!lua_isfunction(state, -1)) luaL_error(state, "%s must be a function", name);
    if (arguments) lua_insert(state, -1 - arguments);
    lua_call(state, arguments, 0);
}

int Runtime::dispatch(lua_State* state)
{
    Runtime& runtime = owner(state);
    switch (runtime.call_) {
    case Call::Setup:
        setup(state);
        break;
    case Call::Start:
        if (luaL_loadbufferx(state, runtime.source_, runtime.sourceLength_,
                            "@app/main.lua", "t") != LUA_OK) return lua_error(state);
        lua_call(state, 0, 0);
        if (!runtime.exitRequested_) callback(state, "on_start", 0);
        break;
    case Call::Event:
        lua_pushstring(state, runtime.eventType_);
        lua_pushstring(state, runtime.eventValue_);
        callback(state, "on_event", 2);
        break;
    case Call::Tick:
        lua_pushinteger(state, static_cast<lua_Integer>(runtime.elapsedMs_));
        callback(state, "on_tick", 1);
        break;
    case Call::Stop:
        callback(state, "on_stop", 0);
        break;
    }
    return 0;
}

int Runtime::show(lua_State* state) { return showView(state, ViewKind::List); }
int Runtime::menu(lua_State* state) { return showView(state, ViewKind::Menu); }
int Runtime::showView(lua_State* state, ViewKind kind)
{
    Runtime& runtime = owner(state);
    View view;
    view.kind = kind;
    if (lua_gettop(state) != 3 && lua_gettop(state) != 4)
        return luaL_error(state, "show expects title, body, items, and optional compact boolean");
    if (lua_gettop(state) == 4) {
        if (!lua_isboolean(state, 4)) return luaL_error(state, "compact must be a boolean");
        view.compact = lua_toboolean(state, 4);
    }
    checkedText(state, 1, view.title, sizeof(view.title), "title");
    checkedText(state, 2, view.body, kind == ViewKind::Menu ? 128 : sizeof(view.body), "body");
    if (!lua_istable(state, 3)) return luaL_error(state, "items must be a table");
    const std::size_t count = lua_rawlen(state, 3);
    if (count > (kind == ViewKind::Menu ? 4u : 8u))
        return luaL_error(state, "too many items for this view");
    // Reject sparse arrays and hidden string-keyed entries, too.
    std::size_t actualCount = 0;
    lua_pushnil(state);
    while (lua_next(state, 3)) {
        if (!lua_isinteger(state, -2) || lua_tointeger(state, -2) < 1 ||
            static_cast<lua_Unsigned>(lua_tointeger(state, -2)) > count)
            return luaL_error(state, "items must be a dense array");
        ++actualCount;
        lua_pop(state, 1);
    }
    if (actualCount != count) return luaL_error(state, "items must be a dense array");
    for (std::size_t i = 0; i < count; ++i) {
        lua_rawgeti(state, 3, static_cast<lua_Integer>(i + 1));
        if (!lua_istable(state, -1)) return luaL_error(state, "each item must be a table");
        lua_getfield(state, -1, "id");
        checkedText(state, -1, view.items[i].id, sizeof(view.items[i].id), "item id");
        lua_pop(state, 1);
        lua_getfield(state, -1, "label");
        checkedText(state, -1, view.items[i].label, sizeof(view.items[i].label), "item label");
        lua_pop(state, 2);
        if (!view.items[i].id[0]) return luaL_error(state, "item id must not be empty");
        for (const char* p = view.items[i].id; *p; ++p) {
            if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                  (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.'))
                return luaL_error(state, "item id contains an invalid character");
        }
        for (std::size_t previous = 0; previous < i; ++previous)
            if (!std::strcmp(view.items[previous].id, view.items[i].id))
                return luaL_error(state, "item ids must be unique");
    }
    view.itemCount = count;
    if (!runtime.exitRequested_) runtime.host_.show(view);
    return 0;
}

int Runtime::exit(lua_State* state)
{
    Runtime& runtime = owner(state);
    if (!runtime.exitRequested_) {
        runtime.exitRequested_ = true;
        runtime.host_.requestExit();
    }
    return 0;
}

int Runtime::uptime(lua_State* state)
{
    lua_pushinteger(state, static_cast<lua_Integer>(owner(state).host_.uptimeMs()));
    return 1;
}

int Runtime::has(lua_State* state)
{
    std::size_t length = 0;
    if (lua_type(state, 1) != LUA_TSTRING) return luaL_error(state, "capability must be a string");
    const char* capability = lua_tolstring(state, 1, &length);
    lua_pushboolean(state, (length == 2 && !std::memcmp(capability, "ui", 2)) ||
                           (length == 6 && !std::memcmp(capability, "system", 6)) ||
                           (length == 9 && !std::memcmp(capability, "player_ui", 9)) ||
                           (length == 5 && !std::memcmp(capability, "audio", 5) && owner(state).host_.audio()));
    return 1;
}

int Runtime::captureBack(lua_State* state) {
    if (lua_gettop(state) != 1 || !lua_isboolean(state, 1))
        return luaL_error(state, "capture_back expects a boolean");
    owner(state).host_.captureBack(lua_toboolean(state, 1));
    return 0;
}

namespace {
lua_Integer boundedInteger(lua_State* state, int index, lua_Integer minimum,
                           lua_Integer maximum, lua_Integer fallback) {
    if (lua_isnoneornil(state, index)) return fallback;
    if (!lua_isinteger(state, index)) luaL_error(state, "argument must be an integer");
    const lua_Integer value = lua_tointeger(state, index);
    if (value < minimum || value > maximum) luaL_error(state, "argument out of range");
    return value;
}
void integerField(lua_State* state, const char* key, lua_Integer value) {
    lua_pushinteger(state, value);
    lua_setfield(state, -2, key);
}
void textField(lua_State* state, const char* key, const char* value) {
    lua_pushstring(state, value);
    lua_setfield(state, -2, key);
}
void pushPage(lua_State* state, const meow::media::Page& page) {
    if (page.count > meow::media::MaxPage) luaL_error(state, "invalid native page");
    lua_createtable(state, 0, 2);
    integerField(state, "total", page.total);
    lua_createtable(state, int(page.count), 0);
    for (size_t i = 0; i < page.count; ++i) {
        lua_createtable(state, 0, 3);
        integerField(state, "id", page.entries[i].id);
        textField(state, "title", page.entries[i].title);
        integerField(state, "count", page.entries[i].count);
        lua_rawseti(state, -2, i + 1);
    }
    lua_setfield(state, -2, "items");
}
}

int Runtime::player(lua_State* state) {
    if (lua_gettop(state) != 1 || !lua_istable(state, 1))
        return luaL_error(state, "player expects a table");
    View view;
    view.kind = ViewKind::Player;
    copyText(view.title, sizeof(view.title), "MP3 Player");
    const auto text = [&](const char* field, char* target, size_t capacity, bool required) {
        lua_getfield(state, 1, field);
        if (required || !lua_isnil(state, -1)) checkedText(state, -1, target, capacity, field);
        lua_pop(state, 1);
    };
    const auto number = [&](const char* field, lua_Integer maximum, lua_Integer fallback) {
        lua_getfield(state, 1, field);
        const auto value = boundedInteger(state, -1, 0, maximum, fallback);
        lua_pop(state, 1);
        return value;
    };
    text("title", view.player.title, sizeof(view.player.title), true);
    text("subtitle", view.player.subtitle, sizeof(view.player.subtitle), false);
    text("status", view.player.status, sizeof(view.player.status), false);
    view.player.position = static_cast<uint32_t>(number("position", UINT32_MAX, 0));
    view.player.duration = static_cast<uint32_t>(number("duration", UINT32_MAX, 0));
    view.player.volume = static_cast<uint8_t>(number("volume", 100, 35));
    lua_getfield(state, 1, "playing");
    if (!lua_isnil(state, -1) && !lua_isboolean(state, -1)) return luaL_error(state, "playing must be a boolean");
    view.player.playing = lua_toboolean(state, -1);
    lua_pop(state, 1);
    const char* ids[] = {"previous", "play", "next", "library", "sound"};
    const char* labels[] = {"Zurueck", "Play / Pause", "Weiter", "Bibliothek", "Klang"};
    view.itemCount = 5;
    for (size_t i = 0; i < 5; ++i) {
        copyText(view.items[i].id, sizeof(view.items[i].id), ids[i]);
        copyText(view.items[i].label, sizeof(view.items[i].label), labels[i]);
    }
    if (!owner(state).exitRequested_) owner(state).host_.show(view);
    return 0;
}

int Runtime::audioCommand(lua_State* state) {
    auto* audio = owner(state).host_.audio();
    if (!audio) return luaL_error(state, "audio capability is not available");
    if (lua_gettop(state) < 1 || lua_gettop(state) > 2) return luaL_error(state, "command expects action and optional value");
    char action[16];
    checkedText(state, 1, action, sizeof(action), "action");
    meow::media::Command command{};
    if (!std::strcmp(action, "scan")) command.action = meow::media::Action::Scan;
    else if (!std::strcmp(action, "pause")) command.action = meow::media::Action::Pause;
    else if (!std::strcmp(action, "stop")) command.action = meow::media::Action::Stop;
    else {
        if (lua_isnoneornil(state, 2)) return luaL_error(state, "this command requires a value");
        if (!std::strcmp(action, "play")) {
            command.action = meow::media::Action::Play;
            command.value = int32_t(boundedInteger(state, 2, 1, 512, 1));
        } else if (!std::strcmp(action, "volume")) {
            command.action = meow::media::Action::Volume;
            command.value = int32_t(boundedInteger(state, 2, 0, 100, 35));
        } else if (!std::strcmp(action, "seek")) {
            command.action = meow::media::Action::Seek;
            command.value = int32_t(boundedInteger(state, 2, 0, 65535, 0));
        } else if (!std::strcmp(action, "eq")) {
            command.action = meow::media::Action::Equalizer;
            command.value = int32_t(boundedInteger(state, 2, 0, 3, 0));
        } else return luaL_error(state, "unknown audio command");
    }
    lua_pushboolean(state, audio->command(command));
    return 1;
}

int Runtime::audioStatus(lua_State* state) {
    auto* audio = owner(state).host_.audio();
    if (!audio) return luaL_error(state, "audio capability is not available");
    meow::media::Status status;
    if (!audio->status(status)) { lua_pushnil(state); return 1; }
    lua_createtable(state, 0, 14);
    textField(state, "state", status.state);
    textField(state, "title", status.title);
    textField(state, "error", status.error);
    integerField(state, "position", status.position);
    integerField(state, "duration", status.duration);
    integerField(state, "generation", status.generation);
    integerField(state, "track", status.track);
    integerField(state, "tracks", status.tracks);
    integerField(state, "playlists", status.playlists);
    integerField(state, "volume", status.volume);
    integerField(state, "finished", status.finished);
    integerField(state, "playback", status.playback);
    integerField(state, "skipped", status.skipped);
    integerField(state, "equalizer", status.equalizer);
    return 1;
}

int Runtime::audioTracks(lua_State* state) {
    auto* audio = owner(state).host_.audio();
    if (!audio) return luaL_error(state, "audio capability is not available");
    const auto playlist = uint16_t(boundedInteger(state, 1, 0, 16, 0));
    const auto offset = size_t(boundedInteger(state, 2, 0, 512, 0));
    const auto limit = size_t(boundedInteger(state, 3, 1, meow::media::MaxPage, meow::media::MaxPage));
    meow::media::Page page;
    if (!audio->tracks(playlist, offset, limit, page)) { lua_pushnil(state); return 1; }
    pushPage(state, page);
    return 1;
}

int Runtime::audioPlaylists(lua_State* state) {
    auto* audio = owner(state).host_.audio();
    if (!audio) return luaL_error(state, "audio capability is not available");
    const auto offset = size_t(boundedInteger(state, 1, 0, 16, 0));
    const auto limit = size_t(boundedInteger(state, 2, 1, meow::media::MaxPage, meow::media::MaxPage));
    meow::media::Page page;
    if (!audio->playlists(offset, limit, page)) { lua_pushnil(state); return 1; }
    pushPage(state, page);
    return 1;
}

}} // namespace meow::luaapps
