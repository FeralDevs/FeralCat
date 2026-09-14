// Exercise the production SD catalog against a fault-injectable host filesystem.
#include "sd_app_catalog.h"
#include "stubs/catalog_host.h"
#include "stubs/esp_heap_caps.h"
#include "../../src/system/usb_msc.h"
#include <cstdio>
#include <cstring>
#include <string>

using meow::luaapps::Manifest;
using meow::luaapps::SdAppCatalog;
using catalog_test::fs;

namespace {
int checks = 0, failures = 0;
void check(bool condition, const char* description) {
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", description); }
}

std::string manifest(const std::string& id, const std::string& name = "Test app") {
    return "id=" + id + "\nname=" + name +
        "\nversion=1.0\napi=1\nentry=main.lua\ncapabilities=ui,system\n";
}

void package(const std::string& id, const std::string& source = "value = 1",
             const std::string& name = "Test app") {
    const std::string path = "/apps/" + id;
    fs.addDirectory(path);
    fs.addFile(path + "/manifest.ini", manifest(id, name));
    fs.addFile(path + "/main.lua", source);
}

void emptyFs() { fs.reset(); fs.addDirectory("/apps"); }

void scan(SdAppCatalog& catalog) {
    catalog.begin();
    std::size_t frames = 0;
    while (catalog.scanning() && frames++ < 70) {
        const std::size_t before = fs.nextCalls;
        catalog.step();
        check(fs.nextCalls - before <= 1, "one directory entry operation per frame");
        check(fs.liveHandles <= 1, "only scan directory survives a frame");
    }
    check(!catalog.scanning(), "scan completes within bounded frame count");
    check(fs.liveHandles == 0, "completed scan closes directory");
}

void balanced() {
    check(fs.liveHandles == 0, "all file handles closed");
    check(fs.handlesOpened == fs.handlesClosed, "each opened file handle closed once");
    check(fs.allocations.empty(), "all source allocations freed");
    check(fs.writeOpens == 0, "catalog opens filesystem read-only");
}

bool contains(const char* value, const char* wanted) { return std::strstr(value, wanted) != nullptr; }

void expectLoadFailure(SdAppCatalog& catalog, const char* description,
                       const char* errorFragment = nullptr) {
    Manifest result{};
    char sentinel = 0;
    char* source = &sentinel;
    std::size_t length = 123;
    check(!catalog.load(0, result, source, length), description);
    check(source == nullptr && length == 0, "failed load clears source and length outputs");
    check(fs.liveHandles == 0 && fs.allocations.empty(), "failed load releases files and source");
    if (errorFragment) check(contains(catalog.status(), errorFragment), "load failure diagnostic");
}

void lifecycle() {
    fs.reset();
    {
        SdAppCatalog catalog;
        catalog.begin();
        check(!catalog.scanning() && catalog.count() == 0, "missing apps directory is empty");
        check(contains(catalog.status(), "/apps"), "missing directory diagnostic");
        catalog.step(); catalog.close(); catalog.close();
    }
    balanced();

    fs.reset(); fs.addFile("/apps", "not a directory");
    { SdAppCatalog catalog; scan(catalog); check(catalog.count() == 0, "apps path must be directory"); }
    balanced();

    emptyFs();
    { SdAppCatalog catalog; scan(catalog); check(catalog.count() == 0, "empty directory supported"); }
    balanced();

    emptyFs(); package("alpha"); package("beta");
    {
        SdAppCatalog catalog;
        catalog.begin();
        check(catalog.scanning() && fs.liveHandles == 1, "begin holds only apps directory");
        catalog.step();
        check(catalog.count() == 1 && fs.liveHandles == 1, "one package discovered in one step");
        Manifest result{}; char* source = nullptr; std::size_t length = 0;
        const std::size_t opens = fs.openedPaths.size();
        check(!catalog.load(0, result, source, length), "launch forbidden during scan");
        check(fs.openedPaths.size() == opens, "rejected in-scan launch performs no filesystem calls");
        catalog.begin();
        check(catalog.count() == 0 && fs.liveHandles == 1, "restart closes old cursor and clears entries");
        catalog.step();
        catalog.invalidate("Scan cancelled");
        check(!catalog.scanning() && catalog.count() == 0 && fs.liveHandles == 0,
              "invalidate cancels and discards partial entries");
        check(!std::strcmp(catalog.status(), "Scan cancelled"), "invalidate keeps reason");
        catalog.step(); catalog.close();
        scan(catalog);
        check(catalog.count() == 2, "rescan recovers after cancellation");
        check(!catalog.load(2, result, source, length), "index at count rejected");
        check(!catalog.load(static_cast<std::size_t>(-1), result, source, length), "overflow-sized index rejected");
    }
    balanced();

    emptyFs(); package("alpha");
    { SdAppCatalog catalog; catalog.begin(); check(fs.liveHandles == 1, "destructor test owns cursor"); }
    balanced();
}

void namesAndOrdering() {
    emptyFs();
    package("zeta", "", "A display name"); // Invalid: empty source.
    package("beta", "x=2", "Z display name");
    package("alpha", "x=1", "Y display name");
    package("gamma", "x=3");
    fs.nodes.at("/apps/beta")->name = "beta"; // File.name() may be basename or full path.
    fs.nodes.at("/apps")->entries.push_back("/apps/alpha"); // Duplicate directory enumeration.
    fs.addFile("/apps/readme.txt", "not a package");
    for (const std::string name : {"../escape", "..", ".", "Bad", "a%2fetc", "a\\b", "a:b", "1bad"}) {
        fs.addDirectory("/apps/invalid_" + std::to_string(fs.nodes.size()), name);
    }
    fs.addDirectory("/apps/mismatch");
    fs.addFile("/apps/mismatch/manifest.ini", manifest("alpha"));
    fs.addFile("/apps/mismatch/main.lua", "x=1");
    {
        SdAppCatalog catalog; scan(catalog);
        check(catalog.count() == 3, "invalid and duplicate folders rejected; regular files ignored");
        check(!std::strcmp(catalog.at(0).id, "alpha") && !std::strcmp(catalog.at(1).id, "beta") &&
              !std::strcmp(catalog.at(2).id, "gamma"), "catalog sorted by stable package ID");
        for (const auto& path : fs.openedPaths) {
            check(path.find("/../") == std::string::npos && path.find('\\') == std::string::npos,
                  "validated paths never contain traversal");
        }
    }
    balanced();

    emptyFs();
    const std::string maxId = "a" + std::string(30, 'b');
    package(maxId);
    package(maxId + "b");
    { SdAppCatalog catalog; scan(catalog); check(catalog.count() == 1, "31-byte folder ID accepted, 32 rejected"); }
    balanced();
}

void manifestAndSourceBounds() {
    emptyFs();
    package("ok", std::string(65536, '-'));
    std::string boundary = manifest("ok");
    boundary += "#" + std::string(2048 - boundary.size() - 1, 'x');
    fs.addFile("/apps/ok/manifest.ini", boundary);
    package("large_manifest");
    fs.addFile("/apps/large_manifest/manifest.ini", boundary + "x");
    package("empty_manifest"); fs.addFile("/apps/empty_manifest/manifest.ini", "");
    package("missing_manifest"); fs.erase("/apps/missing_manifest/manifest.ini");
    package("directory_manifest"); fs.addDirectory("/apps/directory_manifest/manifest.ini");
    package("bad_manifest"); fs.addFile("/apps/bad_manifest/manifest.ini", manifest("bad_manifest") + "unknown=x\n");
    package("short_manifest"); fs.nodes.at("/apps/short_manifest/manifest.ini")->readLimit = 4;
    package("nul_manifest"); fs.addFile("/apps/nul_manifest/manifest.ini", manifest("nul_manifest") + std::string("#\0bad", 5));
    package("large_source", std::string(65537, '-'));
    package("empty_source", "");
    package("missing_source"); fs.erase("/apps/missing_source/main.lua");
    package("directory_source"); fs.addDirectory("/apps/directory_source/main.lua");
    {
        SdAppCatalog catalog; scan(catalog);
        check(catalog.count() == 1 && !std::strcmp(catalog.at(0).id, "ok"),
              "only exact-boundary valid package discovered");
        Manifest result{}; char* source = nullptr; std::size_t length = 0;
        check(catalog.load(0, result, source, length), "64 KiB script and 2048-byte manifest load");
        check(length == 65536 && fs.allocations.size() == 1 && fs.allocations.at(source) == 65536,
              "source allocation exactly matches bounded bytes");
        check(source && source[0] == '-' && source[65535] == '-', "entire boundary script read");
        check(fs.lastCapabilities == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT), "source requests PSRAM");
        heap_caps_free(source);
    }
    balanced();
}

void limits() {
    emptyFs();
    for (int i = 19; i >= 0; --i) package("app" + std::to_string(i));
    {
        SdAppCatalog catalog; scan(catalog);
        check(catalog.count() == 16 && fs.nextCalls == 16, "capacity stops after 16 accepted packages");
        check(contains(catalog.status(), "Limit reached"), "capacity limit reported");
        for (std::size_t i = 1; i < catalog.count(); ++i)
            check(std::strcmp(catalog.at(i - 1).id, catalog.at(i).id) < 0, "bounded catalog remains sorted");
    }
    balanced();

    emptyFs();
    for (int i = 0; i < 64; ++i) fs.addFile("/apps/file" + std::to_string(i), "ignored");
    package("beyond_limit");
    {
        SdAppCatalog catalog; scan(catalog);
        check(catalog.count() == 0 && fs.nextCalls == 64, "64-entry work cap also counts ordinary files");
        check(contains(catalog.status(), "Limit reached"), "entry limit reported");
    }
    balanced();
}

void launchChangesAndFailures() {
    emptyFs(); package("alpha", "old source", "Old name");
    {
        SdAppCatalog catalog; scan(catalog);
        fs.addFile("/apps/alpha/manifest.ini", manifest("alpha", "Updated name") + "memory_kb=64\n");
        const std::string updated = "-- new source\nvalue=42\n";
        fs.addFile("/apps/alpha/main.lua", updated);
        Manifest result{}; char* source = nullptr; std::size_t length = 0;
        check(catalog.load(0, result, source, length), "launch re-reads edited package");
        check(!std::strcmp(result.name, "Updated name") && result.memoryBytes == 64 * 1024,
              "launch uses fresh manifest fields and memory quota");
        check(length == updated.size() && source && !std::memcmp(source, updated.data(), length),
              "launch uses fresh exact script bytes");
        check(fs.liveHandles == 0, "successful launch retains no files");
        heap_caps_free(source);

        fs.addFile("/apps/alpha/manifest.ini", manifest("other"));
        expectLoadFailure(catalog, "changed package ID rejected", "match");
        fs.addFile("/apps/alpha/manifest.ini", manifest("alpha") + "entry=../main.lua\n");
        expectLoadFailure(catalog, "invalid updated manifest rejected");
        fs.erase("/apps/alpha/manifest.ini");
        expectLoadFailure(catalog, "removed manifest rejected", "manifest.ini");
        fs.addFile("/apps/alpha/manifest.ini", manifest("alpha"));
        fs.nodes.at("/apps/alpha/manifest.ini")->readLimit = 3;
        expectLoadFailure(catalog, "short manifest read rejected", "read failed");
        fs.addFile("/apps/alpha/manifest.ini", manifest("alpha"));

        fs.erase("/apps/alpha/main.lua");
        expectLoadFailure(catalog, "removed script rejected", "main.lua");
        fs.addFile("/apps/alpha/main.lua", std::string(65537, 'x'));
        expectLoadFailure(catalog, "script enlarged after scan rejected", "main.lua");
        fs.addDirectory("/apps/alpha/main.lua");
        expectLoadFailure(catalog, "script replaced by directory rejected", "main.lua");
        fs.addFile("/apps/alpha/main.lua", "restored");
        fs.failNextAllocation = true;
        expectLoadFailure(catalog, "PSRAM allocation failure rejected", "PSRAM");
        fs.nodes.at("/apps/alpha/main.lua")->readLimit = 2;
        expectLoadFailure(catalog, "short script read frees allocation", "read failed");
        fs.addFile("/apps/alpha/main.lua", "restored");
        fs.nodes.at("/apps/alpha/main.lua")->failOpenOnAttempt = 2;
        expectLoadFailure(catalog, "file lost between validation and source open rejected", "changed");

        fs.addFile("/apps/alpha/main.lua", "recovered");
        check(catalog.load(0, result, source, length), "valid app still loads after all failed attempts");
        heap_caps_free(source);
        fs.erase("/apps/alpha");
        scan(catalog);
        check(catalog.count() == 0, "removed package disappears on rescan");
    }
    balanced();
}

void usbExclusion() {
    emptyFs(); package("alpha");
    {
        SdAppCatalog catalog;
        fs.msc = true;
        catalog.begin();
        check(!catalog.scanning() && catalog.count() == 0, "begin blocked in USB storage mode");
        check(fs.openedPaths.empty(), "USB-blocked begin never opens SD files");
        fs.msc = false;
        catalog.begin(); catalog.step();
        fs.msc = true;
        catalog.step();
        check(!catalog.scanning() && catalog.count() == 0 && fs.liveHandles == 0,
              "USB detection aborts scan and clears partial catalog");
        fs.msc = false;
        scan(catalog);
        fs.msc = true;
        expectLoadFailure(catalog, "launch blocked in USB mode", "USB");
        fs.msc = false;

        // The native USB service invokes this hook before SD_MMC.end(); this
        // test checks catalog cleanup through that contract, not TinyUSB itself.
        usb_msc_set_before_enable([](void* context) {
            static_cast<SdAppCatalog*>(context)->invalidate("USB storage enabled");
        }, &catalog);
        catalog.begin(); catalog.step();
        check(fs.liveHandles == 1, "USB hook test starts with live scan cursor");
        check(usb_msc_enable() == 1, "test USB transition succeeds");
        check(!fs.enabledWithOpenHandles && fs.liveHandles == 0, "pre-enable hook closes cursor before unmount");
        check(catalog.count() == 0 && !catalog.scanning(), "pre-enable hook invalidates cached packages");
        catalog.step();
        check(fs.filesystemCallsDuringMsc == 0, "no catalog filesystem operation while USB storage active");
        usb_msc_disable();
        usb_msc_set_before_enable(nullptr, nullptr);
        scan(catalog);
        check(catalog.count() == 1, "rescan recovers after USB disconnect");
    }
    balanced();
}
} // namespace

int main() {
    lifecycle();
    namesAndOrdering();
    manifestAndSourceBounds();
    limits();
    launchChangesAndFailures();
    usbExclusion();
    std::printf("catalog_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
