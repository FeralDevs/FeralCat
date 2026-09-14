#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <map>

#define FILE_READ "r"
namespace mock {
struct Node {
    bool directory = false;
    std::string path, content, leafOverride;
    std::vector<std::string> children;
};
struct Disk {
    std::map<std::string, Node> nodes;
    size_t opens = 0, nextCalls = 0, entries = 0, reads = 0, bytes = 0;
    size_t live = 0, peakLive = 0, closes = 0, writes = 0;
    size_t readLimit = SIZE_MAX, failReadAt = SIZE_MAX;
    void directory(const std::string& path);
    void file(const std::string& path, const std::string& text = "");
    size_t io() const { return opens + nextCalls + reads; }
};
struct Handle;
}
class File {
public:
    File() = default;
    explicit File(std::shared_ptr<mock::Handle> handle) : handle_(std::move(handle)) {}
    explicit operator bool() const;
    bool isDirectory() const;
    const char* name() const;
    size_t size() const;
    size_t read(uint8_t* result, size_t length);
    bool seek(uint32_t position);
    File openNextFile();
    void close();
private:
    std::shared_ptr<mock::Handle> handle_;
};
namespace fs {
class FS {
public:
    explicit FS(mock::Disk& disk) : disk_(disk) {}
    File open(const char* path, const char* mode);
private:
    mock::Disk& disk_;
};
}
