#pragma once
#include <cstdint>

namespace meow::audio {
// ES8311 data sheet, rev.10, register 0x32: 0xBF = 0 dB, 0.5 dB/step.
// Player policy: -12 dB. This is a fixed attenuation, not the legacy 0..100 scale.
constexpr int PlayerDacGainDb = -12;
constexpr std::uint8_t DacVolumeRegister = 0x32;
enum class DacGainResult { Ok, InvalidArgument, WriteFailed, ReadFailed, Mismatch };

// Bus must expose I2C_Class-compatible checked byte-write / bulk-read methods.
// Use the checked read, since readRegister8() returns zero for both errors and data.
template<class Bus>
DacGainResult setDacGainDbVerified(Bus& bus, std::uint8_t address, int gainDb)
{
    // Whole dB only; never enable positive codec gain through this API.
    if (gainDb < -95 || gainDb > 0) return DacGainResult::InvalidArgument;
    const auto expected = static_cast<std::uint8_t>(0xBF + gainDb * 2);
    if (!bus.writeRegister8(address, DacVolumeRegister, expected)) return DacGainResult::WriteFailed;
    std::uint8_t actual = 0;
    if (!bus.readRegister(address, DacVolumeRegister, &actual, 1)) return DacGainResult::ReadFailed;
    return actual == expected ? DacGainResult::Ok : DacGainResult::Mismatch;
}
} // namespace meow::audio
