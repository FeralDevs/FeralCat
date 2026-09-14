// Host-only Arduino File test double; the catalog under test is unchanged.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

#define FILE_READ "r"

namespace catalog_test { struct Handle; }
class File {
public:
    File() = default;
    explicit File(std::shared_ptr<catalog_test::Handle> handle);
    explicit operator bool() const;
    bool isDirectory() const;
    const char* name() const;
    std::size_t size() const;
    std::size_t read(std::uint8_t* destination, std::size_t length);
    File openNextFile();
    void close();
private:
    std::shared_ptr<catalog_test::Handle> handle_;
};
