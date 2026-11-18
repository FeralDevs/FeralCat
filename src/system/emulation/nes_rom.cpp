#include "nes_rom.h"

#include <cstring>

namespace meow { namespace nes {
namespace {
bool addBounded(std::size_t& total, std::size_t amount)
{
    if (total > MaxRomContainerBytes || amount > MaxRomContainerBytes - total)
        return false;
    total += amount;
    return true;
}
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size)
{
    if (!data) return 0;
    std::uint32_t value = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        value ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ (0xEDB88320u & (0u - (value & 1u)));
    }
    return value ^ 0xFFFFFFFFu;
}

RomResult inspectRomHeader(const std::uint8_t* header, std::size_t headerBytes,
                           std::size_t fileBytes, RomInfo& out)
{
    out = RomInfo();
    if (!header) return RomResult::InvalidArgument;
    if (fileBytes > MaxRomContainerBytes) return RomResult::ContainerTooLarge;
    if (headerBytes < 16 || fileBytes < 16) return RomResult::TruncatedHeader;
    if (std::memcmp(header, "NES\x1A", 4) != 0) return RomResult::InvalidMagic;
    if ((header[7] & 0x0Cu) == 0x08u) return RomResult::UnsupportedNes2;

    RomInfo parsed;
    parsed.diskDude = std::memcmp(header + 7, "DiskDude!", 9) == 0;
    // DiskDude overwrites every metadata byte from 7 onwards. Treat those as
    // absent legacy metadata, rather than allowing text to become a mapper,
    // SRAM size or PAL flag. Keep flags in byte 6, which were not overwritten.
    const std::uint8_t flags7 = parsed.diskDude ? 0 : header[7];
    const std::uint8_t ramBanks = parsed.diskDude ? 0 : header[8];
    const std::uint8_t regionByte = parsed.diskDude ? 0 : header[9];
    if ((flags7 & 0x0Cu) != 0) return RomResult::UnsupportedHeader;
    if (!parsed.diskDude && (header[12] || header[13] || header[14] || header[15]))
        return RomResult::UnsupportedHeader;
    if ((flags7 & 3u) != 0) return RomResult::UnsupportedConsole;
    if (!header[4]) return RomResult::EmptyPrg;

    parsed.mapper = static_cast<std::uint16_t>((flags7 & 0xF0u) | (header[6] >> 4));
    parsed.region = (regionByte & 1u) ? Region::Pal : Region::Ntsc;
    parsed.trainer = (header[6] & 4u) != 0;
    parsed.battery = (header[6] & 2u) != 0;
    parsed.vertical = (header[6] & 1u) != 0;
    parsed.fourScreen = (header[6] & 8u) != 0;
    parsed.sramBytes = static_cast<std::size_t>(ramBanks ? ramBanks : 1u) * 8192u;
    if (parsed.sramBytes > MaxSramBytes) return RomResult::SramTooLarge;
    parsed.trainerOffset = parsed.trainer ? 16u : 0u;
    parsed.trainerBytes = parsed.trainer ? 512u : 0u;
    parsed.prgBytes = static_cast<std::size_t>(header[4]) * 16384u;
    parsed.chrBytes = static_cast<std::size_t>(header[5]) * 8192u;

    std::size_t total = 16;
    if (!addBounded(total, parsed.trainerBytes)) return RomResult::DeclaredSizeTooLarge;
    parsed.prgOffset = total;
    if (!addBounded(total, parsed.prgBytes)) return RomResult::DeclaredSizeTooLarge;
    parsed.chrOffset = total;
    if (!addBounded(total, parsed.chrBytes)) return RomResult::DeclaredSizeTooLarge;
    if (fileBytes < total) return RomResult::TruncatedPayload;
    parsed.expectedBytes = total;
    parsed.trailingBytes = fileBytes - total;
    out = parsed;
    return RomResult::Ok;
}

RomResult parseRom(const std::uint8_t* data, std::size_t size, RomInfo& out)
{
    const RomResult result = inspectRomHeader(data, size, size, out);
    if (result != RomResult::Ok) return result;
    out.romCrc32 = crc32(data + out.prgOffset, out.prgBytes + out.chrBytes);
    if (out.trainer) out.trainerCrc32 = crc32(data + out.trainerOffset, out.trainerBytes);
    return RomResult::Ok;
}

const char* romResultMessage(RomResult result)
{
    switch (result) {
    case RomResult::Ok: return "Ready";
    case RomResult::InvalidArgument: return "ROM buffer is unavailable";
    case RomResult::TruncatedHeader: return "ROM header is shorter than 16 bytes";
    case RomResult::InvalidMagic: return "Not an iNES ROM (NES header missing)";
    case RomResult::ContainerTooLarge: return "ROM file exceeds the 2 MiB limit";
    case RomResult::UnsupportedNes2: return "NES 2.0 ROMs are not supported yet";
    case RomResult::UnsupportedHeader: return "Unrecognized legacy iNES header";
    case RomResult::UnsupportedConsole: return "VS System and PlayChoice ROMs are not supported";
    case RomResult::EmptyPrg: return "ROM declares no PRG data";
    case RomResult::DeclaredSizeTooLarge: return "Declared ROM data exceeds the 2 MiB limit";
    case RomResult::TruncatedPayload: return "ROM is truncated (declared data is missing)";
    case RomResult::SramTooLarge: return "Declared SRAM exceeds the 64 KiB limit";
    }
    return "Invalid ROM result";
}

}} // namespace meow::nes
