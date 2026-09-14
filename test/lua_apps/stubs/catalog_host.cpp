#include "catalog_host.h"
#include "SD_MMC.h"
#include "esp_heap_caps.h"
#include "../../../src/system/usb_msc.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace catalog_test {
Model fs;
struct Handle {
    std::shared_ptr<Node> node;
    std::size_t cursor = 0, position = 0;
    bool closed = false;
    explicit Handle(std::shared_ptr<Node> value) : node(std::move(value)) {
        ++fs.liveHandles;
        ++fs.handlesOpened;
        fs.maxHandles = (std::max)(fs.maxHandles, fs.liveHandles);
    }
    void close() {
        if (closed) return;
        closed = true;
        --fs.liveHandles;
        ++fs.handlesClosed;
    }
    ~Handle() { close(); }
};

void Model::reset() {
    if (liveHandles || !allocations.empty())
        throw std::runtime_error("test tried to reset with live catalog resources");
    *this = Model{};
}

static void appendToParent(Model& model, const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string::npos || slash == 0) return;
    const auto parent = model.nodes.find(path.substr(0, slash));
    if (parent != model.nodes.end() && parent->second->directory) {
        auto& entries = parent->second->entries;
        if (std::find(entries.begin(), entries.end(), path) == entries.end())
            entries.push_back(path);
    }
}

Node& Model::addDirectory(const std::string& path, const std::string& name) {
    auto node = std::make_shared<Node>();
    node->directory = true;
    node->path = path;
    node->name = name.empty() ? path : name;
    nodes[path] = node;
    appendToParent(*this, path);
    return *node;
}

Node& Model::addFile(const std::string& path, const std::string& contents) {
    auto node = std::make_shared<Node>();
    node->path = node->name = path;
    node->contents = contents;
    nodes[path] = node;
    appendToParent(*this, path);
    return *node;
}

void Model::erase(const std::string& path) {
    nodes.erase(path);
    for (auto& entry : nodes) {
        auto& children = entry.second->entries;
        children.erase(std::remove(children.begin(), children.end(), path), children.end());
    }
}

void Model::touchFilesystem() { if (msc) ++filesystemCallsDuringMsc; }

File Model::open(const std::string& path) {
    touchFilesystem();
    openedPaths.push_back(path);
    const auto found = nodes.find(path);
    if (found == nodes.end()) return {};
    auto& node = found->second;
    ++node->openAttempts;
    if (node->failOpenOnAttempt && node->openAttempts == node->failOpenOnAttempt) return {};
    return File(std::make_shared<Handle>(node));
}
} // namespace catalog_test

File::File(std::shared_ptr<catalog_test::Handle> handle) : handle_(std::move(handle)) {}
File::operator bool() const { return handle_ && !handle_->closed; }
bool File::isDirectory() const {
    catalog_test::fs.touchFilesystem();
    return *this && handle_->node->directory;
}
const char* File::name() const {
    catalog_test::fs.touchFilesystem();
    return *this ? handle_->node->name.c_str() : "";
}
std::size_t File::size() const {
    catalog_test::fs.touchFilesystem();
    return *this ? handle_->node->contents.size() : 0;
}
std::size_t File::read(std::uint8_t* destination, std::size_t length) {
    catalog_test::fs.touchFilesystem();
    ++catalog_test::fs.reads;
    if (!*this || handle_->node->directory) return 0;
    const auto& node = *handle_->node;
    const std::size_t available = node.contents.size() - handle_->position;
    const std::size_t count = (std::min)((std::min)(length, available), node.readLimit);
    std::memcpy(destination, node.contents.data() + handle_->position, count);
    handle_->position += count;
    return count;
}
File File::openNextFile() {
    auto& fs = catalog_test::fs;
    fs.touchFilesystem();
    ++fs.nextCalls;
    if (!*this || !handle_->node->directory) return {};
    const auto& entries = handle_->node->entries;
    while (handle_->cursor < entries.size()) {
        const std::string path = entries[handle_->cursor++];
        File result = fs.open(path);
        if (result) return result;
    }
    return {};
}
void File::close() {
    if (handle_) handle_->close();
    handle_.reset();
}

TestSdMmc SD_MMC;
File TestSdMmc::open(const char* path, const char* mode) {
    if (!mode || std::strcmp(mode, FILE_READ)) ++catalog_test::fs.writeOpens;
    return catalog_test::fs.open(path ? path : "");
}

void* heap_caps_malloc(std::size_t size, unsigned capabilities) {
    auto& fs = catalog_test::fs;
    fs.lastCapabilities = capabilities;
    if (fs.failNextAllocation) { fs.failNextAllocation = false; return nullptr; }
    void* pointer = std::malloc(size);
    if (pointer) fs.allocations[pointer] = size;
    return pointer;
}
void heap_caps_free(void* pointer) {
    if (!pointer) return;
    auto& allocations = catalog_test::fs.allocations;
    if (allocations.erase(pointer) != 1) throw std::runtime_error("unowned or double source free");
    std::free(pointer);
}

extern "C" int usb_msc_is_active() { return catalog_test::fs.msc ? 1 : 0; }
extern "C" void usb_msc_set_before_enable(void (*callback)(void*), void* context) {
    catalog_test::fs.beforeEnable = callback;
    catalog_test::fs.beforeEnableContext = context;
}
extern "C" int usb_msc_enable() {
    auto& fs = catalog_test::fs;
    if (fs.msc) return 1;
    if (fs.beforeEnable) fs.beforeEnable(fs.beforeEnableContext);
    fs.enabledWithOpenHandles = fs.liveHandles != 0;
    fs.msc = true;
    return 1;
}
extern "C" void usb_msc_disable() { catalog_test::fs.msc = false; }
extern "C" unsigned long usb_msc_bytes_transferred() { return 0; }
