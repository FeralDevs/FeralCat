/** Manifest parser; independent of Arduino, the filesystem and the Lua VM. */
#include "app_manifest.h"

#include <cstring>

namespace meow {
namespace luaapps {
namespace {

struct Slice {
    const char* data;
    std::size_t size;
};

void writeError(char* error, std::size_t capacity, const char* message) {
    if (!error || capacity == 0) return;
    std::size_t i = 0;
    while (i + 1 < capacity && message[i]) {
        error[i] = message[i];
        ++i;
    }
    error[i] = '\0';
}

bool fail(char* error, std::size_t capacity, const char* message) {
    writeError(error, capacity, message);
    return false;
}

Slice trim(Slice value) {
    while (value.size && value.data[0] == ' ') {
        ++value.data;
        --value.size;
    }
    while (value.size && value.data[value.size - 1] == ' ') --value.size;
    return value;
}

bool equals(Slice value, const char* literal) {
    const std::size_t length = std::strlen(literal);
    return value.size == length && std::memcmp(value.data, literal, length) == 0;
}

template <std::size_t N>
bool copyValue(char (&destination)[N], Slice value) {
    if (value.size == 0 || value.size >= N) return false;
    std::memcpy(destination, value.data, value.size);
    destination[value.size] = '\0';
    return true;
}

// Reject malformed UTF-8, overlong encodings, surrogates, C0/C1 controls and
// DEL. Only LF and a CR immediately followed by LF are allowed as separators.
bool validDocument(const char* data, std::size_t length) {
    std::size_t i = 0;
    while (i < length) {
        const unsigned char lead = static_cast<unsigned char>(data[i++]);
        if (lead < 0x80) {
            if (lead == '\n') continue;
            if (lead == '\r') {
                if (i >= length || data[i] != '\n') return false;
                continue;
            }
            if (lead < 0x20 || lead == 0x7f) return false;
            continue;
        }

        std::uint32_t codepoint;
        std::uint32_t minimum;
        std::size_t continuation;
        if (lead >= 0xc2 && lead <= 0xdf) {
            codepoint = lead & 0x1f;
            minimum = 0x80;
            continuation = 1;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            codepoint = lead & 0x0f;
            minimum = 0x800;
            continuation = 2;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            codepoint = lead & 0x07;
            minimum = 0x10000;
            continuation = 3;
        } else {
            return false;
        }
        if (continuation > length - i) return false;
        while (continuation--) {
            const unsigned char byte = static_cast<unsigned char>(data[i++]);
            if ((byte & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (byte & 0x3f);
        }
        if (codepoint < minimum || codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff) ||
            (codepoint >= 0x80 && codepoint <= 0x9f)) return false;
    }
    return true;
}

bool validCapabilities(Slice value, std::uint32_t& capabilities) {
    unsigned int seen = 0;
    while (true) {
        std::size_t comma = 0;
        while (comma < value.size && value.data[comma] != ',') ++comma;
        const Slice token = trim(Slice{value.data, comma});
        const unsigned int bit = equals(token, "ui") ? CapUi :
                                 equals(token, "system") ? CapSystem :
                                 equals(token, "audio") ? CapAudio : 0u;
        if (bit == 0 || (seen & bit)) return false;
        seen |= bit;
        if (comma == value.size) {
            capabilities = seen;
            return (seen & (CapUi | CapSystem)) == (CapUi | CapSystem);
        }
        value.data += comma + 1;
        value.size -= comma + 1;
    }
}

bool parseMemory(Slice value, std::uint32_t& bytes) {
    if (value.size == 0) return false;
    std::uint32_t kilobytes = 0;
    for (std::size_t i = 0; i < value.size; ++i) {
        if (value.data[i] < '0' || value.data[i] > '9') return false;
        kilobytes = kilobytes * 10 + static_cast<unsigned int>(value.data[i] - '0');
        // Bound each step before a later multiplication could overflow.
        if (kilobytes > 256) return false;
    }
    if (kilobytes < 64) return false;
    bytes = kilobytes * 1024;
    return true;
}

}  // namespace

bool validateAppId(const char* id, std::size_t length) {
    if (!id || length == 0 || length > 31 || id[0] < 'a' || id[0] > 'z') return false;
    for (std::size_t i = 1; i < length; ++i) {
        const char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

bool parseManifest(const char* data, std::size_t length, Manifest& output,
                   char* error, std::size_t errorSize) {
    writeError(error, errorSize, "");
    if (!data || length == 0 || length > kManifestMaxBytes) {
        return fail(error, errorSize, "Manifest must contain 1..2048 bytes");
    }
    if (!validDocument(data, length)) {
        return fail(error, errorSize, "Manifest contains invalid UTF-8 or control characters");
    }

    enum Key { Id, Name, Version, Api, Entry, Capabilities, Memory, Icon, KeyCount };
    static const char* const keys[] = {
        "id", "name", "version", "api", "entry", "capabilities", "memory_kb", "icon"
    };
    const unsigned int required = (1u << Memory) - 1u;
    unsigned int seen = 0;
    Manifest parsed = {};
    parsed.memoryBytes = 256 * 1024;
    std::size_t cursor = 0;
    while (cursor < length) {
        const std::size_t start = cursor;
        while (cursor < length && data[cursor] != '\n') ++cursor;
        std::size_t lineLength = cursor - start;
        if (lineLength && data[start + lineLength - 1] == '\r') --lineLength;
        if (cursor < length) ++cursor;
        const Slice line = trim(Slice{data + start, lineLength});
        if (line.size == 0 || line.data[0] == '#') continue;

        std::size_t separator = 0;
        while (separator < line.size && line.data[separator] != '=') ++separator;
        if (separator == line.size) return fail(error, errorSize, "Expected key=value");
        const Slice key = trim(Slice{line.data, separator});
        const Slice value = trim(Slice{line.data + separator + 1, line.size - separator - 1});
        unsigned int keyIndex = 0;
        while (keyIndex < KeyCount && !equals(key, keys[keyIndex])) ++keyIndex;
        if (keyIndex == KeyCount) return fail(error, errorSize, "Unknown manifest key");
        const unsigned int bit = 1u << keyIndex;
        if (seen & bit) return fail(error, errorSize, "Duplicate manifest key");
        seen |= bit;

        switch (keyIndex) {
            case Id:
                if (!validateAppId(value.data, value.size)) return fail(error, errorSize, "Invalid app id");
                copyValue(parsed.id, value);
                break;
            case Name:
                if (!copyValue(parsed.name, value)) return fail(error, errorSize, "Name must contain 1..63 UTF-8 bytes");
                break;
            case Version:
                if (!copyValue(parsed.version, value)) return fail(error, errorSize, "Version must contain 1..31 printable ASCII bytes");
                for (std::size_t i = 0; i < value.size; ++i) {
                    const unsigned char c = static_cast<unsigned char>(value.data[i]);
                    if (c < 0x20 || c > 0x7e) return fail(error, errorSize, "Version must be printable ASCII");
                }
                break;
            case Api:
                if (!equals(value, "1")) return fail(error, errorSize, "Unsupported API version");
                parsed.api = 1;
                break;
            case Entry:
                if (!equals(value, "main.lua")) return fail(error, errorSize, "Entry must be main.lua");
                copyValue(parsed.entry, value);
                break;
            case Capabilities:
                if (!validCapabilities(value, parsed.capabilities))
                    return fail(error, errorSize, "Capabilities require ui,system; audio is optional; no duplicates");
                break;
            case Memory:
                if (!parseMemory(value, parsed.memoryBytes)) return fail(error, errorSize, "memory_kb must be an integer from 64 to 256");
                break;
            case Icon:
                if (!equals(value, "app") && !equals(value, "music"))
                    return fail(error, errorSize, "Icon must be app or music");
                parsed.musicIcon = equals(value, "music");
                break;
        }
    }
    if ((seen & required) != required) return fail(error, errorSize, "Missing required manifest key");
    output = parsed;
    return true;
}

}  // namespace luaapps
}  // namespace meow
