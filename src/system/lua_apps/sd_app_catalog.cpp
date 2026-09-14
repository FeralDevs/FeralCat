#include "sd_app_catalog.h"
#include "../usb_msc.h"
#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <cstdio>
#include <utility>

namespace meow::luaapps {
void SdAppCatalog::close() {
    directory_.close();
    scanning_ = false;
}

void SdAppCatalog::invalidate(const char* reason) {
    close();
    count_ = 0;
    snprintf(status_, sizeof(status_), "%s", reason);
}

void SdAppCatalog::begin() {
    close();
    count_ = inspected_ = rejected_ = 0;
    firstError_[0] = 0;
    if (usb_msc_is_active()) {
        snprintf(status_, sizeof(status_), "USB storage is active. Disconnect it before reading apps.");
        return;
    }
    directory_ = SD_MMC.open("/apps", FILE_READ);
    if (!directory_ || !directory_.isDirectory()) {
        close();
        snprintf(status_, sizeof(status_), "No /apps folder. Copy an app folder to the SD card, then Rescan.");
        return;
    }
    scanning_ = true;
    snprintf(status_, sizeof(status_), "Reading /apps ...");
}

bool SdAppCatalog::readManifest(const char* id, Manifest& result) {
    if (usb_msc_is_active()) {
        snprintf(status_, sizeof(status_), "SD card is in USB storage mode.");
        return false;
    }
    if (!validateAppId(id, strlen(id))) {
        snprintf(status_, sizeof(status_), "Invalid app folder ID.");
        return false;
    }
    char path[80];
    snprintf(path, sizeof(path), "/apps/%s/manifest.ini", id);
    File file = SD_MMC.open(path, FILE_READ);
    const size_t size = file ? file.size() : 0;
    if (!file || file.isDirectory() || size == 0 || size > 2048) {
        file.close();
        snprintf(status_, sizeof(status_), "Missing or oversized manifest.ini (max 2048 bytes).");
        return false;
    }
    const size_t got = file.read(reinterpret_cast<uint8_t*>(manifestText_), size);
    file.close();
    if (got != size) {
        snprintf(status_, sizeof(status_), "SD read failed. Check the card and rescan.");
        return false;
    }
    if (!parseManifest(manifestText_, size, result, status_, sizeof(status_))) return false;
    if (strcmp(result.id, id) != 0) {
        snprintf(status_, sizeof(status_), "Manifest ID must match its app folder.");
        return false;
    }
    snprintf(path, sizeof(path), "/apps/%s/main.lua", id);
    file = SD_MMC.open(path, FILE_READ);
    const bool valid = file && !file.isDirectory() && file.size() > 0 && file.size() <= 65536;
    file.close();
    if (!valid) snprintf(status_, sizeof(status_), "Missing or oversized main.lua (max 64 KiB).");
    return valid;
}

void SdAppCatalog::finish(bool limited) {
    close();
    // Stable ordering by package ID, independent of FAT directory order or name.
    for (size_t i = 1; i < count_; ++i)
        for (size_t j = i; j > 0 && strcmp(apps_[j - 1].id, apps_[j].id) > 0; --j)
            std::swap(apps_[j - 1], apps_[j]);
    snprintf(status_, sizeof(status_), "%u apps; %u invalid.%s %s", unsigned(count_), unsigned(rejected_),
             limited ? " Limit reached." : "",
             firstError_[0] ? firstError_ : "Select an app or Rescan.");
}

void SdAppCatalog::step() {
    if (!scanning_) return;
    if (usb_msc_is_active()) {
        close();
        count_ = 0;
        snprintf(status_, sizeof(status_), "USB storage is active. Rescan after disconnecting.");
        return;
    }
    if (inspected_ >= 64 || count_ == Capacity) { finish(true); return; }
    File file = directory_.openNextFile();
    if (!file) { finish(); return; }
    ++inspected_;
    const bool isDir = file.isDirectory();
    const char* name = file.name();
    const char* base = strrchr(name, '/');
    base = base ? base + 1 : name;
    char id[32]{};
    const bool validId = validateAppId(base, strlen(base));
    if (validId) snprintf(id, sizeof(id), "%s", base);
    file.close();
    if (!isDir) return;
    if (!validId || !readManifest(id, apps_[count_])) {
        ++rejected_;
        if (!firstError_[0]) snprintf(firstError_, sizeof(firstError_), "%s: %.75s",
            validId ? id : "Folder", validId ? status_ : "Invalid app folder ID.");
        return;
    }
    for (size_t i = 0; i < count_; ++i) {
        if (strcmp(apps_[i].id, id) == 0) { ++rejected_; return; }
    }
    ++count_;
}

bool SdAppCatalog::load(size_t index, Manifest& manifest, char*& source, size_t& length) {
    source = nullptr;
    length = 0;
    if (scanning_ || index >= count_) return false;
    if (!readManifest(apps_[index].id, manifest)) return false;
    char path[80];
    snprintf(path, sizeof(path), "/apps/%s/main.lua", manifest.id);
    File file = SD_MMC.open(path, FILE_READ);
    const size_t size = file ? file.size() : 0;
    if (!file || file.isDirectory() || size == 0 || size > 65536) {
        file.close();
        snprintf(status_, sizeof(status_), "App changed or SD card was removed. Rescan.");
        return false;
    }
    source = static_cast<char*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!source) {
        file.close();
        snprintf(status_, sizeof(status_), "Not enough PSRAM to load this app.");
        return false;
    }
    const size_t got = file.read(reinterpret_cast<uint8_t*>(source), size);
    file.close();
    if (got != size) {
        heap_caps_free(source);
        source = nullptr;
        snprintf(status_, sizeof(status_), "SD read failed. Check the card and rescan.");
        return false;
    }
    length = size;
    return true;
}
} // namespace meow::luaapps
