/** Bounded manifest parsing for SD-installed MeowKit Lua applications. */
#pragma once

#include <cstddef>
#include <cstdint>

namespace meow {
namespace luaapps {

constexpr std::size_t kManifestMaxBytes = 2048;
constexpr std::size_t kScriptMaxBytes = 64 * 1024;
constexpr std::uint32_t CapUi = 1, CapSystem = 2, CapAudio = 4;

struct Manifest {
    char id[32];
    char name[64];
    char version[32];
    char entry[16];
    std::uint32_t api;
    std::uint32_t memoryBytes;
    std::uint32_t capabilities;
    bool musicIcon;
};

// ID is 1..31 ASCII bytes: [a-z][a-z0-9_-]*. No terminator is required.
bool validateAppId(const char* id, std::size_t length);

// Flat UTF-8 key=value file, with LF or CRLF, optional surrounding spaces,
// blank lines, and whole-line # comments. Tabs and other controls are rejected.
// Required: id, name, version (printable ASCII), api=1, entry=main.lua,
// capabilities=ui,system (either order, exactly once each), optional audio.
// Optional: memory_kb, decimal 64..256, default 256; icon=app|music.
// Unknown/duplicate keys, sections, invalid UTF-8 and NUL bytes are rejected.
// Does not access files or allocate heap memory. The caller checks folder ID,
// script existence and script size separately. Output is unchanged on failure.
// error may be null; otherwise it is NUL-terminated if errorSize is nonzero.
bool parseManifest(const char* data, std::size_t length, Manifest& output,
                   char* error, std::size_t errorSize);

}  // namespace luaapps
}  // namespace meow
