#include "nes_rom.h"
#include "nes_save.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

using namespace meow::nes;

static unsigned checks = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) { \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); std::exit(1); \
} } while (0)

static std::vector<std::uint8_t> rom(unsigned prg, unsigned chr, unsigned mapper,
                                     bool trainer = false)
{
    std::vector<std::uint8_t> data(16 + (trainer ? 512u : 0u) + prg * 16384u + chr * 8192u);
    std::memcpy(data.data(), "NES\x1A", 4);
    data[4] = static_cast<std::uint8_t>(prg);
    data[5] = static_cast<std::uint8_t>(chr);
    data[6] = static_cast<std::uint8_t>((mapper & 15u) << 4 | (trainer ? 4u : 0u));
    data[7] = static_cast<std::uint8_t>(mapper & 240u);
    for (std::size_t i = 16; i < data.size(); ++i)
        data[i] = static_cast<std::uint8_t>(i * 13u + 17u);
    return data;
}

static void testRomIdentityAndLegacy()
{
    auto clean = rom(2, 1, 0);
    clean[6] = 3; // Existing battery and mirroring bits must survive DiskDude.
    RomInfo original;
    CHECK(parseRom(clean.data(), clean.size(), original) == RomResult::Ok);
    CHECK(original.mapper == 0 && original.prgBytes == 32768 && original.chrBytes == 8192);
    CHECK(original.expectedBytes == 40976 && original.trailingBytes == 0);
    CHECK(original.battery && original.vertical && !original.fourScreen);
    CHECK(original.sramBytes == 8192 && original.region == Region::Ntsc);

    auto legacy = clean;
    std::memcpy(legacy.data() + 7, "DiskDude!", 9);
    RomInfo recovered;
    CHECK(parseRom(legacy.data(), legacy.size(), recovered) == RomResult::Ok);
    CHECK(recovered.diskDude && recovered.mapper == 0 && recovered.region == Region::Ntsc);
    CHECK(recovered.sramBytes == 8192 && recovered.battery && recovered.vertical);
    CHECK(recovered.romCrc32 == original.romCrc32);
    CHECK(std::memcmp(legacy.data() + 7, "DiskDude!", 9) == 0); // Original is immutable.
    legacy[15] = '?';
    CHECK(parseRom(legacy.data(), legacy.size(), recovered) == RomResult::UnsupportedHeader);
    CHECK(recovered.prgBytes == 0 && recovered.romCrc32 == 0);

    auto gxrom = rom(2, 1, 66);
    gxrom[9] = 1;
    CHECK(parseRom(gxrom.data(), gxrom.size(), recovered) == RomResult::Ok);
    CHECK(recovered.mapper == 66 && !recovered.diskDude && recovered.region == Region::Pal);

    auto mmc5 = rom(16, 32, 5);
    CHECK(parseRom(mmc5.data(), mmc5.size(), original) == RomResult::Ok);
    CHECK(original.mapper == 5 && original.expectedBytes == 524304);
    mmc5.push_back(0);
    mmc5.push_back(0xA7); // Trailing bytes are reported, not hashed as cartridge content.
    CHECK(parseRom(mmc5.data(), mmc5.size(), recovered) == RomResult::Ok);
    CHECK(recovered.trailingBytes == 2 && recovered.romCrc32 == original.romCrc32);
    mmc5[16] ^= 1;
    CHECK(parseRom(mmc5.data(), mmc5.size(), recovered) == RomResult::Ok);
    CHECK(recovered.romCrc32 != original.romCrc32);
}

static void testRomBoundsAndFeatures()
{
    auto data = rom(1, 0, 2, true);
    data[6] |= 8; // Four-screen mapping is separate from the vertical flag.
    RomInfo info;
    CHECK(parseRom(data.data(), data.size(), info) == RomResult::Ok);
    CHECK(info.trainer && info.trainerOffset == 16 && info.trainerBytes == 512);
    CHECK(info.prgOffset == 528 && info.chrOffset == data.size() && info.chrBytes == 0);
    CHECK(info.fourScreen && !info.vertical);
    const std::uint32_t initialRom = info.romCrc32, initialTrainer = info.trainerCrc32;
    data[16] ^= 1;
    CHECK(parseRom(data.data(), data.size(), info) == RomResult::Ok);
    CHECK(info.romCrc32 == initialRom && info.trainerCrc32 != initialTrainer);
    CHECK(parseRom(data.data(), data.size() - 1, info) == RomResult::TruncatedPayload);
    CHECK(info.expectedBytes == 0);
    for (std::size_t size = 0; size < 16; ++size)
        CHECK(parseRom(data.data(), size, info) == RomResult::TruncatedHeader);
    CHECK(inspectRomHeader(data.data(), 15, data.size(), info) == RomResult::TruncatedHeader);
    CHECK(inspectRomHeader(data.data(), 16, data.size(), info) == RomResult::Ok);
    CHECK(info.romCrc32 == 0 && info.trainerCrc32 == 0);
    CHECK(inspectRomHeader(data.data(), 16, MaxRomContainerBytes + 1, info) == RomResult::ContainerTooLarge);
    CHECK(inspectRomHeader(data.data(), 16, (std::numeric_limits<std::size_t>::max)(), info) == RomResult::ContainerTooLarge);
    CHECK(parseRom(nullptr, 100, info) == RomResult::InvalidArgument);

    auto header = rom(1, 1, 0);
    header[0] = 0;
    CHECK(parseRom(header.data(), header.size(), info) == RomResult::InvalidMagic);
    header[0] = 'N';
    header[7] = 8;
    CHECK(parseRom(header.data(), header.size(), info) == RomResult::UnsupportedNes2);
    header[7] = 1;
    CHECK(parseRom(header.data(), header.size(), info) == RomResult::UnsupportedConsole);
    header[7] = 0;
    header[12] = 1;
    CHECK(parseRom(header.data(), header.size(), info) == RomResult::UnsupportedHeader);
    header[12] = 0;
    header[4] = 0;
    CHECK(parseRom(header.data(), header.size(), info) == RomResult::EmptyPrg);
    header[4] = 255;
    header[5] = 255;
    CHECK(inspectRomHeader(header.data(), 16, MaxRomContainerBytes, info) == RomResult::DeclaredSizeTooLarge);
    header[4] = 127;
    header[5] = 0;
    CHECK(inspectRomHeader(header.data(), 16, MaxRomContainerBytes, info) == RomResult::Ok);
    header[4] = 128; // Exactly 2 MiB PRG still needs its container header.
    CHECK(inspectRomHeader(header.data(), 16, MaxRomContainerBytes, info) == RomResult::DeclaredSizeTooLarge);
    header[4] = 1;
    header[5] = 1;
    header[8] = 8;
    CHECK(parseRom(header.data(), header.size(), info) == RomResult::Ok && info.sramBytes == MaxSramBytes);
    header[8] = 9;
    CHECK(parseRom(header.data(), header.size(), info) == RomResult::SramTooLarge);
}

static void testSaveWireFormatAndRoundTrip()
{
    const std::uint8_t payload[] = {'1','2','3','4','5','6','7','8','9'};
    CHECK(crc32(payload, sizeof(payload)) == 0xCBF43926u);
    CHECK(crc32(nullptr, 0) == 0);
    std::uint8_t output[33] = {};
    std::size_t written = 99;
    CHECK(encodeSave(0x12345678u, payload, sizeof(payload), output, sizeof(output), written) == SaveResult::Ok);
    CHECK(written == sizeof(output));
    const std::uint8_t expectedHeader[24] = {
        'F','C','N','S', 1,0, 24,0, 0x78,0x56,0x34,0x12,
        9,0,0,0, 0x26,0x39,0xF4,0xCB, 0,0,0,0
    };
    CHECK(std::memcmp(output, expectedHeader, sizeof(expectedHeader)) == 0);
    SaveView view;
    CHECK(decodeSave(0x12345678u, output, written, view) == SaveResult::Ok);
    CHECK(view.payloadBytes == 9 && view.romCrc32 == 0x12345678u);
    CHECK(std::memcmp(view.payload, payload, sizeof(payload)) == 0);

    // Integration layout: the service reads core SRAM directly into the
    // reserved payload area, then packs the header into the same allocation.
    std::uint8_t reserved[SaveHeaderBytes + sizeof(payload)] = {};
    std::memcpy(reserved + SaveHeaderBytes, payload, sizeof(payload));
    CHECK(encodeSave(0x12345678u, reserved + SaveHeaderBytes, sizeof(payload),
                     reserved, sizeof(reserved), written) == SaveResult::Ok);
    CHECK(std::memcmp(reserved, output, sizeof(output)) == 0);
    CHECK(decodeSave(0x12345678u, reserved, written, view) == SaveResult::Ok);
    reserved[SaveHeaderBytes + 3] ^= 1;
    CHECK(decodeSave(0x12345678u, reserved, written, view) == SaveResult::ChecksumMismatch);
    CHECK(view.payload == nullptr);

    std::vector<std::uint8_t> maximum(MaxSaveBytes, 0x9A);
    CHECK(encodeSave(0, maximum.data(), MaxSramBytes, maximum.data(), maximum.size(), written) == SaveResult::Ok);
    CHECK(written == MaxSaveBytes);
    CHECK(decodeSave(0, maximum.data(), written, view) == SaveResult::Ok);
    CHECK(view.payloadBytes == MaxSramBytes && view.payload[0] == 0x9A && view.payload[MaxSramBytes - 1] == 0x9A);
}

static void testSaveRejectsCorruption()
{
    std::vector<std::uint8_t> payload(8192, 0x55), encoded(SaveHeaderBytes + payload.size());
    std::size_t written;
    CHECK(encodeSave(123, payload.data(), payload.size(), encoded.data(), encoded.size(), written) == SaveResult::Ok);
    SaveView view;
    CHECK(decodeSave(123, encoded.data(), encoded.size(), view) == SaveResult::Ok);
    CHECK(decodeSave(124, encoded.data(), encoded.size(), view) == SaveResult::RomMismatch);
    CHECK(view.payload == nullptr && view.payloadBytes == 0);
    for (std::size_t size = 0; size < SaveHeaderBytes; ++size)
        CHECK(decodeSave(123, encoded.data(), size, view) == SaveResult::TruncatedHeader);
    CHECK(decodeSave(123, encoded.data(), encoded.size() - 1, view) == SaveResult::LengthMismatch);
    encoded.push_back(0);
    CHECK(decodeSave(123, encoded.data(), encoded.size(), view) == SaveResult::LengthMismatch);
    encoded.pop_back();
    struct Mutation { std::size_t offset; std::uint8_t value; SaveResult result; };
    const Mutation cases[] = {
        {0, 0, SaveResult::InvalidMagic}, {4, 2, SaveResult::UnsupportedVersion},
        {6, 23, SaveResult::InvalidHeader}, {20, 1, SaveResult::InvalidHeader},
        {15, 255, SaveResult::InvalidPayloadSize}, {16, 0, SaveResult::ChecksumMismatch},
        {24, 0, SaveResult::ChecksumMismatch}
    };
    for (const auto& mutation : cases) {
        auto corrupt = encoded;
        corrupt[mutation.offset] = mutation.value;
        CHECK(decodeSave(123, corrupt.data(), corrupt.size(), view) == mutation.result);
        CHECK(view.payload == nullptr);
    }
    auto empty = encoded;
    std::memset(empty.data() + 12, 0, 4);
    CHECK(decodeSave(123, empty.data(), empty.size(), view) == SaveResult::InvalidPayloadSize);
    CHECK(decodeSave(123, nullptr, 24, view) == SaveResult::InvalidArgument);
    std::uint8_t guard[24];
    std::memset(guard, 0xA5, sizeof(guard));
    CHECK(encodeSave(123, payload.data(), payload.size(), guard, sizeof(guard), written) == SaveResult::OutputTooSmall);
    CHECK(written == 0 && guard[0] == 0xA5 && guard[23] == 0xA5);
    CHECK(encodeSave(123, payload.data(), 0, guard, sizeof(guard), written) == SaveResult::InvalidPayloadSize);
    CHECK(encodeSave(123, payload.data(), MaxSramBytes + 1, guard, sizeof(guard), written) == SaveResult::InvalidPayloadSize);
    CHECK(encodeSave(123, nullptr, 1, guard, sizeof(guard), written) == SaveResult::InvalidArgument);
}

int main()
{
    testRomIdentityAndLegacy();
    testRomBoundsAndFeatures();
    testSaveWireFormatAndRoundTrip();
    testSaveRejectsCorruption();
    std::printf("NES storage: %u checks passed\n", checks);
    return 0;
}
