// Fault-injectable in-memory filesystem and allocation accounting for tests.
#pragma once
#include "FS.h"
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace catalog_test {
struct Node {
    bool directory = false;
    std::string path, name, contents;
    std::vector<std::string> entries;
    std::size_t readLimit = (std::numeric_limits<std::size_t>::max)();
    std::size_t openAttempts = 0, failOpenOnAttempt = 0;
};

struct Model {
    std::map<std::string, std::shared_ptr<Node>> nodes;
    std::vector<std::string> openedPaths;
    std::size_t liveHandles = 0, maxHandles = 0, handlesOpened = 0, handlesClosed = 0;
    std::size_t reads = 0, nextCalls = 0, filesystemCallsDuringMsc = 0, writeOpens = 0;
    std::map<void*, std::size_t> allocations;
    bool msc = false, failNextAllocation = false;
    unsigned lastCapabilities = 0;
    void (*beforeEnable)(void*) = nullptr;
    void* beforeEnableContext = nullptr;
    bool enabledWithOpenHandles = false;

    void reset();
    Node& addDirectory(const std::string& path, const std::string& name = "");
    Node& addFile(const std::string& path, const std::string& contents);
    void erase(const std::string& path);
    void touchFilesystem();
    File open(const std::string& path);
};
extern Model fs;
} // namespace catalog_test
