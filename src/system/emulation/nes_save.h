#pragma once

#include "nes_rom.h"

namespace meow { namespace nes {

static const std::size_t SaveHeaderBytes = 24;
static const std::size_t MaxSaveBytes = SaveHeaderBytes + MaxSramBytes;
enum class SaveResult {
    Ok, InvalidArgument, InvalidPayloadSize, OutputTooSmall, TruncatedHeader,
    InvalidMagic, UnsupportedVersion, InvalidHeader, LengthMismatch,
    RomMismatch, ChecksumMismatch
};

struct SaveView {
    const std::uint8_t* payload = nullptr;
    std::size_t payloadBytes = 0;
    std::uint32_t romCrc32 = 0;
};

// Wire format: "FCNS", u16 version=1, u16 headerBytes=24, u32 ROM CRC,
// u32 payloadBytes, u32 payload CRC, u32 reserved=0, then SRAM payload.
// All integers are little endian. No native structs are serialized.
// Empty SRAM is not a save. Both functions allocate nothing.
SaveResult encodeSave(std::uint32_t romCrc, const std::uint8_t* payload,
                      std::size_t payloadBytes, std::uint8_t* out,
                      std::size_t outCapacity, std::size_t& written);
// Returned view borrows the input. On any failure the view is reset. The caller
// must also compare payloadBytes against the cartridge's actual SRAM size.
SaveResult decodeSave(std::uint32_t expectedRomCrc, const std::uint8_t* data,
                      std::size_t size, SaveView& out);
const char* saveResultMessage(SaveResult result);

}} // namespace meow::nes
