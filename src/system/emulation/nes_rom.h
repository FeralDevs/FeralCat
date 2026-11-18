#pragma once

#include <cstddef>
#include <cstdint>

namespace meow { namespace nes {

static const std::size_t MaxRomContainerBytes = 2u * 1024u * 1024u;
static const std::size_t MaxSramBytes = 64u * 1024u;

enum class Region { Ntsc, Pal };
enum class RomResult {
    Ok, InvalidArgument, TruncatedHeader, InvalidMagic, ContainerTooLarge,
    UnsupportedNes2, UnsupportedHeader, UnsupportedConsole, EmptyPrg,
    DeclaredSizeTooLarge, TruncatedPayload, SramTooLarge
};

struct RomInfo {
    std::uint16_t mapper = 0;
    Region region = Region::Ntsc;
    bool trainer = false;
    bool battery = false;
    bool vertical = false;
    bool fourScreen = false;
    bool diskDude = false;
    std::size_t trainerOffset = 0;
    std::size_t trainerBytes = 0;
    std::size_t prgOffset = 0;
    std::size_t prgBytes = 0;
    std::size_t chrOffset = 0;
    std::size_t chrBytes = 0;
    std::size_t sramBytes = 0;
    std::size_t expectedBytes = 0;
    std::size_t trailingBytes = 0;
    // Canonical PRG+CHR CRC, excluding header, trainer and trailing bytes.
    // Computed only by parseRom; inspectRomHeader leaves both CRCs at zero.
    std::uint32_t romCrc32 = 0;
    std::uint32_t trainerCrc32 = 0;
};

// All functions are allocation-free. Call inspectRomHeader with a small header
// read and the actual file size before allocating or reading the full ROM.
// Output is reset on failure. Only exact "DiskDude!" at bytes 7..15 is repaired;
// the supplied bytes are never changed. NES 2.0 is recognized but unsupported.
RomResult inspectRomHeader(const std::uint8_t* header, std::size_t headerBytes,
                           std::size_t fileBytes, RomInfo& out);
RomResult parseRom(const std::uint8_t* data, std::size_t size, RomInfo& out);
const char* romResultMessage(RomResult result);

// Standard reflected CRC-32/ISO-HDLC (same result as zlib crc32(data)).
// Null is accepted only for the empty input; returns zero for that input.
std::uint32_t crc32(const std::uint8_t* data, std::size_t size);

}} // namespace meow::nes
