#pragma once
#include "app_manifest.h"
#include <FS.h>

namespace meow::luaapps {
// Fixed storage; scanning does at most one directory entry per launcher frame.
class SdAppCatalog {
public:
    static constexpr size_t Capacity = 16;
    void begin();
    void step();
    void close();
    void invalidate(const char* reason);
    bool scanning() const { return scanning_; }
    size_t count() const { return count_; }
    const Manifest& at(size_t index) const { return apps_[index]; }
    const char* status() const { return status_; }
    // Revalidate metadata at launch, so updates/removals never use stale paths.
    bool load(size_t index, Manifest& manifest, char*& source, size_t& length);
private:
    Manifest apps_[Capacity]{};
    File directory_;
    size_t count_ = 0, inspected_ = 0, rejected_ = 0;
    bool scanning_ = false;
    char status_[160]{};
    char firstError_[112]{};
    char manifestText_[2049]{};
    bool readManifest(const char* id, Manifest& result);
    void finish(bool limited = false);
};
} // namespace meow::luaapps
