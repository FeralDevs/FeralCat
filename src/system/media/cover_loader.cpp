#include "cover_loader.h"
#include "media_paths.h"
#include <esp_heap_caps.h>
#include <lgfx/utility/lgfx_tjpgd.h>
#include <algorithm>
#include <cstring>
#if defined(ARDUINO)
#include <Arduino.h>
#else
#include <chrono>
#endif

namespace meow::media {
namespace {
constexpr size_t PathLimit = 191, PoolBytes = 8192, DescriptionLimit = 1024;
uint32_t clockMs()
{
#if defined(ARDUINO)
    return millis();
#else
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
struct Budget {
    CoverHooks hooks;
    uint32_t start;
    CoverResult result = CoverResult::Ready;
    explicit Budget(CoverHooks h) : hooks(h), start(now()) {}
    uint32_t now() const { return hooks.clockMs ? hooks.clockMs(hooks.context) : clockMs(); }
    bool check() {
        if (result != CoverResult::Ready) return false;
        if (hooks.cancel && hooks.cancel(hooks.context)) result = CoverResult::Cancelled;
        else if (static_cast<uint32_t>(now() - start) >= CoverBudgetMs) result = CoverResult::TimedOut;
        return result == CoverResult::Ready;
    }
};
struct Buffer {
    uint8_t* data;
    explicit Buffer(size_t bytes) : data(static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))) {}
    ~Buffer() { if (data) heap_caps_free(data); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
};
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint16_t be16(const uint8_t* p) { return uint16_t(p[0]) << 8 | p[1]; }
bool sync32(const uint8_t* p, uint32_t& out) {
    if ((p[0] | p[1] | p[2] | p[3]) & 0x80) return false;
    out = uint32_t(p[0]) << 21 | uint32_t(p[1]) << 14 | uint32_t(p[2]) << 7 | p[3];
    return true;
}
bool readAt(File& file, size_t offset, uint8_t* out, size_t length, Budget& budget)
{
    if (!budget.check() || offset > file.size() || length > file.size() - offset ||
        offset > UINT32_MAX || !file.seek(static_cast<uint32_t>(offset))) return false;
    while (length) {
        if (!budget.check()) return false;
        const size_t count = file.read(out, (std::min)(length, size_t(512)));
        if (!count || count > length) return false;
        out += count; length -= count;
    }
    return budget.check();
}
CoverResult ioResult(const Budget& b) { return b.result == CoverResult::Ready ? CoverResult::Invalid : b.result; }
bool terminal(CoverResult r) { return r == CoverResult::Ready || r == CoverResult::Cancelled || r == CoverResult::TimedOut || r == CoverResult::NoMemory; }

// TinyJPEG expects complete SOF/SOS/table segments. Validate their boundaries
// before handing it untrusted SD bytes; no alternative decoder or Huffman tree.
CoverResult checkJpeg(const uint8_t* data, size_t size, uint16_t& width, uint16_t& height, Budget& budget)
{
    if (size < 4 || data[0] != 0xff || data[1] != 0xd8) return CoverResult::Invalid;
    size_t pos = 2;
    uint8_t comps = 0, ids[3] = {}, tables[3] = {}, qmask = 0, hmask = 0;
    for (size_t segment = 0; segment < 128 && pos + 4 <= size; ++segment) {
        if (!budget.check()) return budget.result;
        if (data[pos++] != 0xff) return CoverResult::Invalid;
        while (pos < size && data[pos] == 0xff) ++pos;
        if (pos + 3 > size) return CoverResult::Invalid;
        const uint8_t marker = data[pos++];
        const size_t length = be16(data + pos); pos += 2;
        if (length < 2 || length - 2 > size - pos) return CoverResult::Invalid;
        const size_t n = length - 2;
        const uint8_t* p = data + pos; pos += n;
        if (marker == 0xc0) {
            if (comps || n < 6 || p[0] != 8) return CoverResult::Invalid;
            height = be16(p + 1); width = be16(p + 3); comps = p[5];
            if (!width || !height || width > MaxCoverDimension || height > MaxCoverDimension) return CoverResult::Unsupported;
            if ((comps != 1 && comps != 3) || n != size_t(6 + 3 * comps)) return CoverResult::Unsupported;
            for (size_t c = 0; c < comps; ++c) {
                ids[c] = p[6 + 3 * c]; tables[c] = p[8 + 3 * c];
                const uint8_t sampling = p[7 + 3 * c];
                if (tables[c] > 3 || (c ? sampling != 0x11 : sampling != 0x11 && sampling != 0x21 && sampling != 0x22)) return CoverResult::Unsupported;
                for (size_t previous = 0; previous < c; ++previous) if (ids[previous] == ids[c]) return CoverResult::Invalid;
            }
        } else if (marker == 0xdb) {
            if (!n || n > JD_SZBUF || n % 65) return CoverResult::Invalid;
            for (size_t k = 0; k < n; k += 65) {
                if (p[k] > 3 || (qmask & (1 << p[k]))) return CoverResult::Unsupported;
                qmask |= 1 << p[k];
                for (size_t q = 1; q <= 64; ++q) if (!p[k + q]) return CoverResult::Invalid;
            }
        } else if (marker == 0xc4) {
            if (!n || n > JD_SZBUF) return CoverResult::Invalid;
            size_t k = 0;
            while (k < n) {
                if (n - k < 17 || (p[k] & 0xee)) return CoverResult::Invalid;
                const unsigned bit = 1u << ((p[k] >> 4) * 2 + (p[k] & 1));
                if (hmask & bit) return CoverResult::Unsupported;
                hmask |= bit;
                size_t count = 0; int slots = 1;
                for (size_t i = 1; i <= 16; ++i) {
                    count += p[k + i]; slots = slots * 2 - p[k + i];
                    if (slots < 0) return CoverResult::Invalid;
                }
                if (!count || count > 256 || count > n - k - 17) return CoverResult::Invalid;
                k += 17 + count;
            }
        } else if (marker == 0xda) {
            if (!comps || n != size_t(4 + 2 * comps) || p[0] != comps) return CoverResult::Invalid;
            for (size_t c = 0; c < comps; ++c) {
                if (p[1 + 2 * c] != ids[c] || p[2 + 2 * c] != (c ? 0x11 : 0) ||
                    !(qmask & (1 << tables[c])) || (hmask & (c ? 10 : 5)) != (c ? 10 : 5)) return CoverResult::Unsupported;
            }
            if (p[n - 3] || p[n - 2] != 63 || p[n - 1]) return CoverResult::Unsupported;
            // Require an EOI and one baseline scan; inspect at most MaxCoverBytes.
            while (pos < size) {
                if (!(pos & 1023) && !budget.check()) return budget.result;
                if (data[pos++] != 0xff) continue;
                while (pos < size && data[pos] == 0xff) ++pos;
                if (pos == size) return CoverResult::Invalid;
                const uint8_t code = data[pos++];
                if (code == 0xd9) return CoverResult::Ready;
                if (code && (code < 0xd0 || code > 0xd7)) return CoverResult::Invalid;
            }
            return CoverResult::Invalid;
        } else if (marker == 0xdd) {
            if (n != 2) return CoverResult::Invalid;
        } else if (marker != 0xfe && (marker < 0xe0 || marker > 0xef)) {
            return CoverResult::Unsupported;
        }
    }
    return CoverResult::Invalid;
}

struct Decoder {
    const uint8_t* data; size_t size, position = 0;
    uint16_t* pixels; Budget& budget;
    uint32_t sw = 0, sh = 0, dw = 0, dh = 0, ox = 0, oy = 0;
};
uint32_t jpegInput(void* context, uint8_t* out, uint32_t count)
{
    auto& d = *static_cast<Decoder*>(context);
    if (!d.budget.check()) return 0;
    count = static_cast<uint32_t>((std::min)(size_t(count), d.size - d.position));
    if (out) std::memcpy(out, d.data + d.position, count);
    d.position += count;
    return count;
}
uint32_t jpegOutput(void* context, void* bitmap, JRECT* rect)
{
    auto& d = *static_cast<Decoder*>(context);
    if (!d.budget.check()) return 0;
    if (rect->left > rect->right || rect->top > rect->bottom || rect->right >= d.sw || rect->bottom >= d.sh) return 0;
    const auto* rgb = static_cast<const uint8_t*>(bitmap);
    const uint32_t rw = rect->right - rect->left + 1;
    const uint32_t x0 = (rect->left * d.dw + d.sw - 1) / d.sw;
    const uint32_t x1 = ((rect->right + 1) * d.dw + d.sw - 1) / d.sw;
    const uint32_t y0 = (rect->top * d.dh + d.sh - 1) / d.sh;
    const uint32_t y1 = ((rect->bottom + 1) * d.dh + d.sh - 1) / d.sh;
    for (uint32_t y = y0; y < y1; ++y) {
        const uint32_t sy = y * d.sh / d.dh - rect->top;
        for (uint32_t x = x0; x < x1; ++x) {
            const uint32_t sx = x * d.sw / d.dw - rect->left;
            const uint8_t* p = rgb + (sy * rw + sx) * 3;
            d.pixels[(y + d.oy) * CoverWidth + x + d.ox] = uint16_t((p[0] & 0xf8) << 8 | (p[1] & 0xfc) << 3 | p[2] >> 3);
        }
    }
    return 1;
}
CoverResult decode(const uint8_t* data, size_t bytes, uint16_t* pixels, CoverInfo& info, Budget& budget)
{
    uint16_t width = 0, height = 0;
    const CoverResult checked = checkJpeg(data, bytes, width, height, budget);
    if (checked != CoverResult::Ready) return checked;
    Buffer pool(PoolBytes);
    if (!pool.data) return CoverResult::NoMemory;
    Decoder d{data, bytes, 0, pixels, budget};
    lgfxJdec jpeg{}; // The vendored prepare function does not clear table pointers.
    if (lgfx_jd_prepare(&jpeg, jpegInput, pool.data, PoolBytes, &d) != JDR_OK) return ioResult(budget);
    if (jpeg.width != width || jpeg.height != height) return CoverResult::Invalid;
    unsigned scale = 0;
    while (scale < 3 && (width >> (scale + 1)) && (height >> (scale + 1)) &&
           ((std::max)(width, height) >> scale) > 192) ++scale;
    d.sw = width >> scale; d.sh = height >> scale;
    d.dw = d.sw >= d.sh ? CoverWidth : (std::max)(1u, uint32_t(CoverWidth) * d.sw / d.sh);
    d.dh = d.sh >= d.sw ? CoverHeight : (std::max)(1u, uint32_t(CoverHeight) * d.sh / d.sw);
    d.ox = (CoverWidth - d.dw) / 2; d.oy = (CoverHeight - d.dh) / 2;
    std::memset(pixels, 0, CoverPixels * sizeof(uint16_t));
    if (lgfx_jd_decomp(&jpeg, jpegOutput, scale) != JDR_OK || !budget.check()) return ioResult(budget);
    info.width = width; info.height = height;
    return CoverResult::Ready;
}
CoverResult sidecar(fs::FS& fs, const char* path, uint16_t* pixels, CoverInfo& info, Budget& budget)
{
    if (!budget.check()) return budget.result;
    File file = fs.open(path, FILE_READ);
    if (!file || file.isDirectory()) return CoverResult::Missing;
    const size_t size = file.size();
    if (!size || size > MaxCoverBytes) return CoverResult::Unsupported;
    Buffer buffer(size);
    if (!buffer.data) return CoverResult::NoMemory;
    if (!readAt(file, 0, buffer.data, size, budget)) return ioResult(budget);
    file.close();
    return decode(buffer.data, size, pixels, info, budget);
}
CoverResult picture(const uint8_t* data, size_t size, uint16_t* pixels, CoverInfo& info, Budget& budget)
{
    if (size < 5 || data[0] > 3) return CoverResult::Unsupported;
    size_t pos = 1;
    while (pos < size && pos <= 32 && data[pos]) ++pos;
    if (pos >= size || pos > 32) return CoverResult::Invalid;
    if (!((pos == 11 && !std::memcmp(data + 1, "image/jpeg", 10)) || (pos == 10 && !std::memcmp(data + 1, "image/jpg", 9)))) return CoverResult::Unsupported;
    ++pos;
    if (pos >= size || (data[pos] != 3 && data[pos] != 0)) return CoverResult::Unsupported;
    ++pos;
    const size_t end = (std::min)(size, pos + DescriptionLimit);
    const size_t step = data[0] == 1 || data[0] == 2 ? 2 : 1;
    while (pos + step <= end && (data[pos] || (step == 2 && data[pos + 1]))) pos += step;
    if (pos + step > end) return CoverResult::Invalid;
    pos += step;
    if (size - pos > MaxCoverBytes) return CoverResult::Unsupported;
    return decode(data + pos, size - pos, pixels, info, budget);
}
CoverResult embedded(fs::FS& fs, const char* path, uint16_t* pixels, CoverInfo& info, Budget& budget)
{
    if (!budget.check()) return budget.result;
    File file = fs.open(path, FILE_READ);
    if (!file || file.isDirectory() || file.size() < 10) return CoverResult::Missing;
    uint8_t h[10];
    if (!readAt(file, 0, h, sizeof(h), budget)) return ioResult(budget);
    if (std::memcmp(h, "ID3", 3)) return CoverResult::Missing;
    const bool v4 = h[3] == 4;
    // Unsynchronisation and footer tags are deliberately unsupported.
    if ((!v4 && h[3] != 3) || h[4] == 0xff || (h[5] & 0x9f)) return CoverResult::Unsupported;
    uint32_t tagBytes = 0;
    if (!sync32(h + 6, tagBytes)) return CoverResult::Invalid;
    if (tagBytes > MaxCoverTagBytes) return CoverResult::Unsupported;
    const size_t end = 10 + size_t(tagBytes);
    if (end > file.size()) return CoverResult::Invalid;
    size_t pos = 10;
    if (h[5] & 0x40) {
        if (end - pos < 4 || !readAt(file, pos, h, 4, budget)) return ioResult(budget);
        uint32_t ext = be32(h);
        if (v4 && !sync32(h, ext)) return CoverResult::Invalid;
        if (ext < 6 || ext > end - pos) return CoverResult::Invalid;
        const size_t skip = size_t(ext) + (v4 ? 0 : 4);
        if (skip > end - pos) return CoverResult::Invalid;
        pos += skip;
    }
    CoverResult result = CoverResult::Missing;
    for (size_t frame = 0; frame < MaxCoverFrames && end - pos >= 10; ++frame) {
        if (!readAt(file, pos, h, sizeof(h), budget)) return ioResult(budget);
        if (!h[0]) return result;
        for (size_t i = 0; i < 4; ++i) if (!((h[i] >= 'A' && h[i] <= 'Z') || (h[i] >= '0' && h[i] <= '9'))) return CoverResult::Invalid;
        uint32_t bytes = be32(h + 4);
        if (v4 && !sync32(h + 4, bytes)) return CoverResult::Invalid;
        pos += 10;
        if (bytes > end - pos) return CoverResult::Invalid;
        if (!std::memcmp(h, "APIC", 4) && !h[9] && !(h[8] & (v4 ? 0x8f : 0x1f))) {
            if (bytes >= 5 && bytes <= MaxCoverBytes + DescriptionLimit + 40) {
                Buffer buffer(bytes);
                if (!buffer.data) return CoverResult::NoMemory;
                if (!readAt(file, pos, buffer.data, bytes, budget)) return ioResult(budget);
                result = picture(buffer.data, bytes, pixels, info, budget);
                if (terminal(result)) return result;
            } else result = CoverResult::Unsupported;
        }
        pos += bytes;
    }
    return result;
}
} // namespace

CoverResult loadTrackCover(fs::FS& source, const char* trackPath, uint16_t* pixels,
                          size_t capacity, CoverInfo& info, CoverHooks hooks)
{
    info = {};
    if (!trackPath || !pixels || capacity < CoverPixels) return CoverResult::Invalid;
    size_t length = 0;
    while (length <= PathLimit && trackPath[length]) ++length;
    char canonical[PathLimit + 1];
    if (length > PathLimit || !normalizeMediaPath("/mp3", trackPath, length, canonical, sizeof(canonical)) ||
        std::strcmp(canonical, trackPath)) return CoverResult::Invalid;
    char* slash = std::strrchr(canonical, '/');
    if (!slash || slash == canonical || !slash[1]) return CoverResult::Invalid;
    *slash = 0;
    Budget budget(hooks);
    char path[PathLimit + 1];
    const char* names[] = {"cover.jpg", "folder.jpg"};
    for (size_t i = 0; i < 2; ++i) {
        if (!normalizeMediaPath(canonical, names[i], std::strlen(names[i]), path, sizeof(path))) continue;
        const CoverResult result = sidecar(source, path, pixels, info, budget);
        if (result == CoverResult::Ready) info.source = i ? CoverSource::FolderJpeg : CoverSource::CoverJpeg;
        if (terminal(result)) return result;
    }
    const CoverResult result = embedded(source, trackPath, pixels, info, budget);
    if (result == CoverResult::Ready) info.source = CoverSource::EmbeddedJpeg;
    return result;
}
} // namespace meow::media
