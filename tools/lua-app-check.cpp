// Host-only package checker. It exercises the same bounded VM as the device;
// passing is not evidence of display, input, stack or timing behavior on ESP32.
#include "app_manifest.h"
#include "lua_runtime.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;
using namespace meow::luaapps;

namespace {
struct CheckHost : Host {
    View view;
    unsigned views = 0;
    bool exitRequested = false;
    const std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
    void show(const View& value) override { view = value; ++views; }
    std::uint32_t uptimeMs() override
    {
        return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - began).count());
    }
    void requestExit() override { exitRequested = true; }
};

bool readBounded(const fs::path& path, std::size_t maximum, std::string& contents)
{
    std::error_code error;
    // Packages consist of actual files, so a link cannot substitute another
    // package's entry point or escape the directory being checked.
    const auto status = fs::symlink_status(path, error);
    if (error || !fs::is_regular_file(status)) return false;
    const auto length = fs::file_size(path, error);
    if (error || length == 0 || length > maximum) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    contents.assign(static_cast<std::size_t>(length), '\0');
    if (!input.read(&contents[0], static_cast<std::streamsize>(length))) return false;
    return input.peek() == std::char_traits<char>::eof();
}

int check(const fs::path& suppliedDirectory)
{
    std::error_code error;
    const auto directory = fs::canonical(suppliedDirectory, error);
    if (error || !fs::is_directory(directory, error) || error) {
        std::fprintf(stderr, "FAIL: package directory does not exist or is unreadable.\n");
        return 1;
    }
    const auto folderId = directory.filename().string();
    if (!validateAppId(folderId.data(), folderId.size())) {
        std::fprintf(stderr, "FAIL: package directory needs an ID matching [a-z][a-z0-9_-]* (max 31 bytes).\n");
        return 1;
    }
    std::string manifestText;
    if (!readBounded(directory / "manifest.ini", kManifestMaxBytes, manifestText)) {
        std::fprintf(stderr, "FAIL: manifest.ini must be a readable regular file of 1..2048 bytes.\n");
        return 1;
    }
    Manifest manifest = {};
    char diagnostic[192] = {};
    if (!parseManifest(manifestText.data(), manifestText.size(), manifest, diagnostic, sizeof(diagnostic))) {
        std::fprintf(stderr, "FAIL: manifest: %s\n", diagnostic);
        return 1;
    }
    if (folderId != manifest.id) {
        std::fprintf(stderr, "FAIL: manifest id must equal the package directory name.\n");
        return 1;
    }
    std::string source;
    if (!readBounded(directory / manifest.entry, kScriptMaxBytes, source)) {
        std::fprintf(stderr, "FAIL: main.lua must be a readable regular file of 1..65536 bytes.\n");
        return 1;
    }
    Limits limits;
    limits.memoryBytes = manifest.memoryBytes;
    CheckHost host;
    Runtime runtime(host, limits);
    if (!runtime.start(source.data(), source.size())) {
        std::fprintf(stderr, "FAIL: startup: %s\n", runtime.error());
        return 1;
    }
    const auto startupHeap = runtime.memoryUsed();
    const auto startupViews = host.views;
    const auto startupActions = host.view.itemCount;
    runtime.stop();
    if (runtime.error()[0] || runtime.memoryUsed() != 0) {
        std::fprintf(stderr, "FAIL: shutdown: %s\n", runtime.error());
        return 1;
    }
    std::printf("PASS: %s (%s), version %s, API %u.\n", manifest.name, manifest.id, manifest.version,
                static_cast<unsigned>(manifest.api));
    std::printf("Source: %zu bytes. Lua heap after startup: %zu / %u bytes.\n", source.size(), startupHeap,
                static_cast<unsigned>(manifest.memoryBytes));
    std::printf("Startup views: %u; actions in last view: %zu. Shutdown released the Lua heap.\n",
                startupViews, startupActions);
    std::printf("Only manifest, startup and shutdown checked; events and ESP32 behavior need separate tests.\n");
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2 || !std::strcmp(argv[1], "--help") || !std::strcmp(argv[1], "-h")) {
        std::fprintf(argc == 2 ? stdout : stderr, "Usage: app_check <package-directory>\n");
        return argc == 2 ? 0 : 2;
    }
    try {
        return check(fs::path(argv[1]));
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: host file or memory error: %s\n", error.what());
        return 1;
    }
}
