#include "nes_path.h"

#include <cstddef>
#include <cstring>

namespace meow::nes {

bool validPath(const char* path, bool romFile)
{
    if (!path) return false;
    constexpr char Root[] = "/roms/nes";
    constexpr std::size_t RootLength = sizeof(Root) - 1;
    constexpr std::size_t MaxLength = 255;
    std::size_t length = 0;
    while (length <= MaxLength && path[length]) ++length;
    if (length > MaxLength || length < RootLength ||
        std::memcmp(path, Root, RootLength) != 0) return false;
    if (length == RootLength) return !romFile;
    if (path[RootLength] != '/') return false;

    std::size_t segment = RootLength + 1;
    for (std::size_t i = segment; i <= length; ++i) {
        const unsigned char c = static_cast<unsigned char>(path[i]);
        if (c == '/' || c == 0) {
            const std::size_t bytes = i - segment;
            if (!bytes || (bytes == 1 && path[segment] == '.') ||
                (bytes == 2 && path[segment] == '.' && path[segment + 1] == '.')) return false;
            if (c == '/') segment = i + 1;
        } else if (c < 32 || c == 127 || c == '\\' || c == ':') return false;
    }
    if (!romFile) return true;
    const std::size_t leafLength = length - segment;
    return leafLength >= 4 && path[length - 4] == '.' &&
        (path[length - 3] == 'n' || path[length - 3] == 'N') &&
        (path[length - 2] == 'e' || path[length - 2] == 'E') &&
        (path[length - 1] == 's' || path[length - 1] == 'S');
}

} // namespace meow::nes
