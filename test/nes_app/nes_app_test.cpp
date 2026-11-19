// Compile the real SD frontend against deterministic firmware ABI doubles.
#include "mk_app_abi.h"
#include "mk_nes_abi.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

extern "C" int app_main(int, char**);
static void check(bool condition, const char* reason)
{
    if (!condition) { std::cerr << "FAIL: " << reason << '\n'; std::exit(1); }
}

namespace fake {
uint32_t version;
int beginResult, scanFailures, closeFailures, beginCalls, endCalls, runs, polls, releases;
mk_nes_state_t state;
mk_nes_catalog_t catalog;
mk_nes_input_t genericInput;
std::map<std::string, std::vector<mk_nes_entry_t>> folders;
std::deque<mk_nes_input_t> input;
std::deque<int> runResults;
std::vector<std::string> scans, opens, drawn;
std::vector<std::pair<int,int>> commands;

mk_nes_entry_t entry(const std::string& name, bool directory = false)
{
    mk_nes_entry_t result{};
    std::snprintf(result.name, sizeof(result.name), "%s", name.c_str());
    result.flags = directory ? MK_NES_DIRECTORY : MK_NES_HEADER_VALID | MK_NES_SUPPORTED;
    result.bytes = 40976; result.mapper = 0; result.prg_bytes = 32768;
    result.chr_bytes = 8192; result.region = 60;
    return result;
}
void reset()
{
    version = MK_NES_ABI_VERSION; beginResult = MK_NES_OK;
    scanFailures = closeFailures = beginCalls = endCalls = runs = polls = releases = 0;
    state = {}; state.sound = 1; state.sram_bytes = 8192;
    catalog = {}; genericInput = {}; folders.clear(); input.clear(); runResults.clear();
    scans.clear(); opens.clear(); drawn.clear(); commands.clear();
    folders[MK_NES_ROOT] = {entry("Game.nes")};
}
void press(uint32_t buttons) { mk_nes_input_t e{}; e.pressed = e.held = buttons; input.push_back(e); }
void touch(int y) { mk_nes_input_t e{}; e.touching = e.touch_pressed = 1; e.touch_x = 30; e.touch_y = y; input.push_back(e); }
void sleep() { mk_nes_input_t e{}; e.sleep_requested = 1; input.push_back(e); }
void close() { touch(32 + 5*29 + 1); }
void finish()
{
    check(app_main(0, nullptr) == 0, "app failed");
    check(beginCalls == 1 && endCalls == 1 && !state.loaded, "session cleanup contract");
    check(input.empty() && runResults.empty(), "frontend did not consume expected actions");
}
bool saw(const std::string& text)
{
    return std::any_of(drawn.begin(), drawn.end(), [&](const std::string& s) { return s.find(text) != std::string::npos; });
}
}

extern "C" {
uint32_t mk_nes_version(void) { return fake::version; }
int mk_nes_begin(void) { ++fake::beginCalls; return fake::beginResult; }
void mk_nes_end(void) { ++fake::endCalls; fake::state.loaded = 0; }
int mk_nes_catalog_open(const char* path)
{
    check(!fake::state.loaded, "catalog accessed while ROM still loaded");
    fake::scans.emplace_back(path);
    std::snprintf(fake::catalog.path, sizeof(fake::catalog.path), "%s", path);
    fake::catalog.count = 0;
    if (fake::scanFailures > 0) { --fake::scanFailures; return MK_NES_ERROR; }
    auto found = fake::folders.find(path);
    if (found == fake::folders.end()) return MK_NES_ERROR;
    fake::catalog.count = static_cast<uint32_t>(found->second.size());
    return static_cast<int>(fake::catalog.count);
}
void mk_nes_catalog_info(mk_nes_catalog_t* out) { *out = fake::catalog; }
int mk_nes_catalog_entry(uint32_t index, mk_nes_entry_t* out)
{
    const auto& entries = fake::folders[fake::catalog.path];
    check(index < entries.size(), "frontend requested out-of-bounds catalog entry");
    *out = entries[index]; return MK_NES_OK;
}
int mk_nes_open(const char* path)
{
    check(!fake::state.loaded, "ROM replaced before close");
    fake::opens.emplace_back(path); fake::state.loaded = 1;
    return MK_NES_OK;
}
int mk_nes_run(void)
{
    check(fake::state.loaded, "run without ROM");
    check(!fake::runResults.empty(), "unexpected gameplay run");
    ++fake::runs; int result = fake::runResults.front(); fake::runResults.pop_front(); return result;
}
int mk_nes_command(int command, int value)
{
    fake::commands.emplace_back(command, value);
    if (command == MK_NES_SOUND) fake::state.sound = value != 0;
    if (command == MK_NES_CLOSE) {
        if (fake::closeFailures > 0) { --fake::closeFailures; return MK_NES_ERROR; }
        fake::state.loaded = 0;
    }
    return MK_NES_OK;
}
void mk_nes_state(mk_nes_state_t* out) { *out = fake::state; }
void mk_nes_error(char* out, uint32_t bytes) { std::snprintf(out, bytes, "Injected service failure; data retained."); }
void mk_nes_poll(mk_nes_input_t* out)
{
    ++fake::polls;
    check(!fake::input.empty() && fake::polls < 200, "frontend stuck or consumed an extra input");
    *out = fake::input.front(); fake::input.pop_front();
}
void mk_nes_wait_release(void) { ++fake::releases; }
void mk_input_poll(void)
{
    check(fake::beginCalls==0 || fake::endCalls==1, "generic input mixed into an active NES session");
    mk_nes_poll(&fake::genericInput);
}
int mk_btn(int id)
{
    return (fake::genericInput.pressed & (id==MK_BTN_A ? MK_NES_A : id==MK_BTN_B ? MK_NES_B : 0)) != 0;
}
void mk_delay(uint32_t) {}
int mk_snprintf(char* out, size_t count, const char* format, ...)
{
    va_list args; va_start(args, format); int n = std::vsnprintf(out, count, format, args); va_end(args); return n;
}
void mk_gfx_clear(void) {}
void mk_gfx_header(const char* title) { fake::drawn.emplace_back(title); }
void mk_gfx_footer(const char* left, const char* right) { fake::drawn.emplace_back(left); fake::drawn.emplace_back(right); }
void mk_gfx_text(int x, int y, const char* text, uint32_t)
{
    check(x >= 0 && x < 320 && y >= 0 && y < 240, "text outside screen");
    fake::drawn.emplace_back(text);
}
void mk_gfx_fill_round_rect(int x, int y, int w, int h, int, uint32_t)
{
    check(x >= 0 && y >= 0 && x+w <= 320 && y+h <= 240, "menu rectangle outside screen");
}
void mk_gfx_present(void) {}
}

static void foldersAndTouch()
{
    using namespace fake;
    reset();
    folders[MK_NES_ROOT] = {entry("Games", true), entry("Other.nes")};
    folders["/roms/nes/Games"] = {entry("Adventure.nes")};
    press(MK_NES_A); touch(50); close(); press(MK_NES_B); press(MK_NES_B);
    runResults = {MK_NES_PAUSED}; finish();
    check(opens == std::vector<std::string>{"/roms/nes/Games/Adventure.nes"}, "folder/touch opened wrong ROM");
    check(scans == std::vector<std::string>{MK_NES_ROOT, "/roms/nes/Games", MK_NES_ROOT}, "parent navigation escaped root");
    check(commands == std::vector<std::pair<int,int>>{{MK_NES_CLOSE,0}}, "close command missing");
}

static void pagingAndPauseActions()
{
    using namespace fake;
    reset(); folders[MK_NES_ROOT].clear();
    for (int i=0; i<12; ++i) folders[MK_NES_ROOT].push_back(entry("Game" + std::to_string(i) + ".nes"));
    press(MK_NES_RIGHT); press(MK_NES_A); // opens row 5
    press(MK_NES_B); // resume first pause
    press(MK_NES_DOWN); press(MK_NES_A); press(MK_NES_A); // save + acknowledge
    press(MK_NES_DOWN); press(MK_NES_A); // reset and resume
    press(MK_NES_DOWN); press(MK_NES_DOWN); press(MK_NES_DOWN); press(MK_NES_A); // sound off
    press(MK_NES_DOWN); press(MK_NES_A); // sleep, then run signals another power request
    close(); press(MK_NES_B);
    runResults = {MK_NES_PAUSED, MK_NES_PAUSED, MK_NES_PAUSED, MK_NES_SLEEP_REQUESTED, MK_NES_PAUSED};
    finish();
    check(opens == std::vector<std::string>{"/roms/nes/Game5.nes"}, "page navigation opened wrong ROM");
    check(commands == std::vector<std::pair<int,int>>{{MK_NES_SAVE,0},{MK_NES_RESET,0},{MK_NES_SOUND,0},
          {MK_NES_SLEEP,0},{MK_NES_SLEEP,0},{MK_NES_CLOSE,0}}, "pause/sleep command sequence changed");
    check(saw("Sound: off") && saw("SRAM saved."), "pause feedback missing");
}

static void saveFailureRetainsRom()
{
    using namespace fake;
    reset(); closeFailures = 1;
    press(MK_NES_A); close(); press(MK_NES_A); // acknowledge failed save
    press(MK_NES_A); // retry selected Close, do not reload/lose SRAM
    press(MK_NES_B); runResults = {MK_NES_PAUSED}; finish();
    check(opens.size()==1 && runs==1 && commands.size()==2, "failed close discarded/reloaded ROM");
    check(commands[0].first==MK_NES_CLOSE && commands[1].first==MK_NES_CLOSE, "failed close not retried");
    check(saw("Action failed"), "save failure not visible");
}

static void unavailableEmptyAndLongPaths()
{
    using namespace fake;
    reset(); scanFailures = 1; press(MK_NES_A); press(MK_NES_B); finish();
    check(scans.size()==2 && saw("Folder unavailable"), "SD removal/retry missing");
    reset(); folders[MK_NES_ROOT].clear(); press(MK_NES_A); sleep(); press(MK_NES_B); finish();
    check(scans.size()==2 && saw("No subfolders") && commands[0].first==MK_NES_SLEEP, "empty rescan/sleep failed");
    reset(); folders[MK_NES_ROOT] = {entry(std::string(248,'a') + ".nes")};
    press(MK_NES_A); press(MK_NES_A); press(MK_NES_B); finish();
    check(opens.empty() && saw("Path too long"), "frontend silently truncated ROM path");
}

static void startupFailures()
{
    using namespace fake;
    reset(); beginResult = MK_NES_ERROR; press(MK_NES_A);
    check(app_main(0,nullptr)==1 && beginCalls==1 && endCalls==1, "failed begin skipped end");
    reset(); version = 0; press(MK_NES_A);
    check(app_main(0,nullptr)==1 && beginCalls==0 && endCalls==0 && saw("NES update required"), "ABI mismatch started service");
}

int main()
{
    foldersAndTouch(); pagingAndPauseActions(); saveFailureRetainsRom();
    unavailableEmptyAndLongPaths(); startupFailures();
    std::cout << "NES app: browser/paging/touch/pause/sleep/save-failure/SD-retry/path/ABI/cleanup passed\n";
}
