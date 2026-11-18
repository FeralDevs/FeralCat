#include "nes_save.h"

#include <cstring>

namespace meow { namespace nes {
namespace {
std::uint16_t read16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}
std::uint32_t read32(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}
void write16(std::uint8_t* p, std::uint16_t value)
{
    p[0] = static_cast<std::uint8_t>(value);
    p[1] = static_cast<std::uint8_t>(value >> 8);
}
void write32(std::uint8_t* p, std::uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(value >> (i * 8));
}
}

SaveResult encodeSave(std::uint32_t romCrc, const std::uint8_t* payload,
                      std::size_t payloadBytes, std::uint8_t* out,
                      std::size_t outCapacity, std::size_t& written)
{
    written = 0;
    if (!payload || !out) return SaveResult::InvalidArgument;
    if (!payloadBytes || payloadBytes > MaxSramBytes) return SaveResult::InvalidPayloadSize;
    if (outCapacity < SaveHeaderBytes + payloadBytes) return SaveResult::OutputTooSmall;
    const std::uint32_t checksum = crc32(payload, payloadBytes);
    // Moving the payload first also supports packing in-place with header room.
    std::memmove(out + SaveHeaderBytes, payload, payloadBytes);
    std::memcpy(out, "FCNS", 4);
    write16(out + 4, 1);
    write16(out + 6, static_cast<std::uint16_t>(SaveHeaderBytes));
    write32(out + 8, romCrc);
    write32(out + 12, static_cast<std::uint32_t>(payloadBytes));
    write32(out + 16, checksum);
    write32(out + 20, 0);
    written = SaveHeaderBytes + payloadBytes;
    return SaveResult::Ok;
}

SaveResult decodeSave(std::uint32_t expectedRomCrc, const std::uint8_t* data,
                      std::size_t size, SaveView& out)
{
    out = SaveView();
    if (!data) return SaveResult::InvalidArgument;
    if (size < SaveHeaderBytes) return SaveResult::TruncatedHeader;
    if (std::memcmp(data, "FCNS", 4) != 0) return SaveResult::InvalidMagic;
    if (read16(data + 4) != 1) return SaveResult::UnsupportedVersion;
    if (read16(data + 6) != SaveHeaderBytes || read32(data + 20) != 0)
        return SaveResult::InvalidHeader;
    const std::size_t payloadBytes = read32(data + 12);
    if (!payloadBytes || payloadBytes > MaxSramBytes) return SaveResult::InvalidPayloadSize;
    if (size != SaveHeaderBytes + payloadBytes) return SaveResult::LengthMismatch;
    if (read32(data + 8) != expectedRomCrc) return SaveResult::RomMismatch;
    if (crc32(data + SaveHeaderBytes, payloadBytes) != read32(data + 16))
        return SaveResult::ChecksumMismatch;
    out.payload = data + SaveHeaderBytes;
    out.payloadBytes = payloadBytes;
    out.romCrc32 = expectedRomCrc;
    return SaveResult::Ok;
}

const char* saveResultMessage(SaveResult result)
{
    switch (result) {
    case SaveResult::Ok: return "Ready";
    case SaveResult::InvalidArgument: return "Save buffer is unavailable";
    case SaveResult::InvalidPayloadSize: return "Save SRAM size must be 1 to 65536 bytes";
    case SaveResult::OutputTooSmall: return "Save output buffer is too small";
    case SaveResult::TruncatedHeader: return "Save header is truncated";
    case SaveResult::InvalidMagic: return "Not a FeralCat NES SRAM save";
    case SaveResult::UnsupportedVersion: return "Unsupported NES save version";
    case SaveResult::InvalidHeader: return "Invalid NES save header";
    case SaveResult::LengthMismatch: return "NES save length does not match its header";
    case SaveResult::RomMismatch: return "NES save belongs to different ROM content";
    case SaveResult::ChecksumMismatch: return "NES save checksum failed";
    }
    return "Invalid save result";
}

}} // namespace meow::nes
