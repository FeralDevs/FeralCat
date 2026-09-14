#include "FS.h"
#include "esp_heap_caps.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace mock {
Heap heap;
static std::unordered_map<void*, size_t> allocations;
static std::string parent(const std::string& path)
{
    const size_t slash = path.rfind('/');
    return slash ? path.substr(0, slash) : std::string();
}
void Disk::directory(const std::string& path)
{
    if (nodes.count(path)) return;
    const std::string up = parent(path);
    if (!up.empty()) directory(up);
    Node node; node.directory = true; node.path = path;
    nodes.emplace(path, std::move(node));
    if (!up.empty()) nodes.at(up).children.push_back(path);
}
void Disk::file(const std::string& path, const std::string& text)
{
    const std::string up = parent(path);
    if (!up.empty()) directory(up);
    const bool fresh = !nodes.count(path);
    Node node; node.path = path; node.content = text;
    nodes[path] = std::move(node);
    if (fresh && !up.empty()) nodes.at(up).children.push_back(path);
}
struct Handle {
    Disk& disk; Node& node; std::string leaf;
    size_t position = 0, cursor = 0;
    bool closed = false;
    Handle(Disk& disk, Node& node) : disk(disk), node(node)
    {
        leaf = node.leafOverride.empty() ? node.path.substr(node.path.rfind('/') + 1) : node.leafOverride;
        ++disk.live; disk.peakLive = (std::max)(disk.live, disk.peakLive);
    }
    ~Handle() { close(); }
    void close() { if (!closed) { closed = true; --disk.live; ++disk.closes; } }
};
}
void* heap_caps_malloc(size_t size, unsigned caps)
{
    ++mock::heap.calls;
    if (caps != (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) mock::heap.wrongCapabilities = true;
    if (mock::heap.calls == mock::heap.failCall) return nullptr;
    void* value = std::malloc(size);
    if (value) {
        mock::allocations[value] = size; ++mock::heap.live; mock::heap.bytes += size;
        mock::heap.peakBytes = (std::max)(mock::heap.peakBytes, mock::heap.bytes);
    }
    return value;
}
void heap_caps_free(void* pointer)
{
    auto found = mock::allocations.find(pointer);
    if (found == mock::allocations.end()) std::abort();
    --mock::heap.live; mock::heap.bytes -= found->second;
    mock::allocations.erase(found); std::free(pointer);
}
File::operator bool() const { return handle_ && !handle_->closed; }
bool File::isDirectory() const { return *this && handle_->node.directory; }
const char* File::name() const { return *this ? handle_->leaf.c_str() : nullptr; }
size_t File::size() const { return *this ? handle_->node.content.size() : 0; }
void File::close() { if (handle_) handle_->close(); handle_.reset(); }
File File::openNextFile()
{
    if (!isDirectory()) return {};
    auto& h = *handle_; ++h.disk.nextCalls;
    if (h.cursor >= h.node.children.size()) return {};
    ++h.disk.entries;
    return File(std::make_shared<mock::Handle>(h.disk, h.disk.nodes.at(h.node.children[h.cursor++])));
}
size_t File::read(uint8_t* result, size_t length)
{
    if (!*this || isDirectory()) return 0;
    auto& h = *handle_; ++h.disk.reads;
    if (h.disk.reads >= h.disk.failReadAt) return 0;
    const size_t count = (std::min)({length, h.disk.readLimit, h.node.content.size() - h.position});
    std::memcpy(result, h.node.content.data() + h.position, count);
    h.position += count; h.disk.bytes += count;
    return count;
}
bool File::seek(uint32_t position)
{
    if (!*this || isDirectory() || position > size()) return false;
    handle_->position = position;
    return true;
}
File fs::FS::open(const char* path, const char* mode)
{
    ++disk_.opens;
    if (std::strcmp(mode, FILE_READ)) { ++disk_.writes; return {}; }
    auto found = disk_.nodes.find(path);
    if (found == disk_.nodes.end()) return {};
    return File(std::make_shared<mock::Handle>(disk_, found->second));
}
