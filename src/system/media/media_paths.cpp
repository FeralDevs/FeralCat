#include "media_paths.h"
#include <cstring>
#include <cstdint>

namespace meow::media {
namespace {
unsigned char lower(unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
bool separator(char c) { return c == '/' || c == '\\'; }

/* Media library root — single source of truth. A previous /mp3→/music rename
 * missed the hardcoded root length (4) and root name ("mp3") here, which made
 * normalizeMediaPath reject every path and broke all scanning. Derive both from
 * one constant so a future rename can't silently regress it again. */
static constexpr char   MEDIA_ROOT[]   = "/music";
static constexpr size_t MEDIA_ROOT_LEN = sizeof(MEDIA_ROOT) - 1;   /* "/music" = 6 */

bool components(const char* text, size_t length, size_t position, char* out, size_t& used)
{
    while (position < length) {
        while (position < length && separator(text[position])) ++position;
        const size_t start = position;
        while (position < length && !separator(text[position])) ++position;
        const size_t count = position - start;
        if (!count || (count == 1 && text[start] == '.')) continue;
        if (count == 2 && text[start] == '.' && text[start + 1] == '.') {
            if (used == MEDIA_ROOT_LEN) return false;
            while (used > MEDIA_ROOT_LEN && out[used - 1] != '/') --used;
            if (used > MEDIA_ROOT_LEN) --used;
            out[used] = 0;
            continue;
        }
        if (used + count + 1 > 191) return false;
        out[used++] = '/';
        std::memcpy(out + used, text + start, count);
        used += count;
        out[used] = 0;
    }
    return true;
}
bool rootPrefix(const char* text, size_t length, size_t& after)
{
    if (!length || !separator(text[0])) return false;
    size_t pos = 0;
    while (pos < length && separator(text[pos])) ++pos;
    const size_t start = pos;
    while (pos < length && !separator(text[pos])) ++pos;
    const size_t rootName = MEDIA_ROOT_LEN - 1;   /* "music" (root without the leading '/') */
    if (pos - start != rootName) return false;
    for (size_t i = 0; i < rootName; ++i)
        if (lower(static_cast<unsigned char>(text[start + i])) !=
            lower(static_cast<unsigned char>(MEDIA_ROOT[1 + i]))) return false;
    after = pos;
    return true;
}
}

bool validMediaText(const char* text, size_t length)
{
    if (!text) return false;
    for (size_t i = 0; i < length;) {
        const auto first = static_cast<unsigned char>(text[i++]);
        uint32_t code = first;
        unsigned extra = 0;
        if (first >= 0xc2 && first <= 0xdf) { code = first & 31; extra = 1; }
        else if (first >= 0xe0 && first <= 0xef) { code = first & 15; extra = 2; }
        else if (first >= 0xf0 && first <= 0xf4) { code = first & 7; extra = 3; }
        else if (first >= 0x80) return false;
        if (i + extra > length) return false;
        for (unsigned n = 0; n < extra; ++n) {
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) return false;
            code = (code << 6) | (next & 63);
        }
        if ((extra == 1 && code < 0x80) || (extra == 2 && code < 0x800) ||
            (extra == 3 && code < 0x10000) || code > 0x10ffff ||
            (code >= 0xd800 && code <= 0xdfff) || code < 0x20 ||
            (code >= 0x7f && code <= 0x9f)) return false;
    }
    return true;
}

bool normalizeMediaPath(const char* base, const char* text, size_t length,
                        char* result, size_t capacity)
{
    if (!result || !capacity) return false;
    result[0] = 0;
    if (!base || !length || !validMediaText(text, length)) return false;
    // Neither URI schemes nor Windows drive prefixes are local media paths.
    for (size_t i = 0; i < length; ++i) if (text[i] == ':') return false;
    char normalized[192];
    std::memcpy(normalized, MEDIA_ROOT, MEDIA_ROOT_LEN + 1);
    size_t used = MEDIA_ROOT_LEN, position = 0;
    if (separator(text[0])) {
        if (!rootPrefix(text, length, position)) return false;
    } else {
        const size_t baseLength = std::strlen(base);
        if (!validMediaText(base, baseLength) || !rootPrefix(base, baseLength, position) ||
            !components(base, baseLength, position, normalized, used)) return false;
        for (size_t i = 0; i < baseLength; ++i) if (base[i] == ':') return false;
        position = 0;
    }
    if (!components(text, length, position, normalized, used) || used + 1 > capacity) return false;
    std::memcpy(result, normalized, used + 1);
    return true;
}

int compareMediaPaths(const char* left, const char* right)
{
    while (*left && *right) {
        const unsigned a = lower(static_cast<unsigned char>(*left++));
        const unsigned b = lower(static_cast<unsigned char>(*right++));
        if (a != b) return a < b ? -1 : 1;
    }
    return *left ? 1 : (*right ? -1 : 0);
}
} // namespace meow::media
