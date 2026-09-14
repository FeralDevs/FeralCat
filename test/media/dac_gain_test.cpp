#include "dac_gain.hpp"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <initializer_list>
using namespace meow::audio;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #x); std::exit(1); } } while (false)
struct Bus {
    unsigned writes = 0, reads = 0;
    std::uint8_t address = 0, reg = 0, value = 0;
    bool writeOkay = true, readOkay = true, mismatch = false;
    bool writeRegister8(std::uint8_t a, std::uint8_t r, std::uint8_t v) {
        ++writes; address = a; reg = r; value = v; return writeOkay;
    }
    bool readRegister(std::uint8_t a, std::uint8_t r, std::uint8_t* out, std::size_t length) {
        ++reads; CHECK(a == address && r == reg && length == 1);
        *out = mismatch ? static_cast<std::uint8_t>(value ^ 1) : value;
        return readOkay;
    }
};
int main()
{
    CHECK(PlayerDacGainDb == -12 && DacVolumeRegister == 0x32);
    Bus bus;
    CHECK(setDacGainDbVerified(bus, 0x18, PlayerDacGainDb) == DacGainResult::Ok);
    CHECK(bus.address == 0x18 && bus.reg == 0x32 && bus.value == 0xA7);
    CHECK(bus.writes == 1 && bus.reads == 1);
    CHECK(setDacGainDbVerified(bus, 0x18, -76) == DacGainResult::Ok && bus.value == 0x27);
    CHECK(setDacGainDbVerified(bus, 0x18, -24) == DacGainResult::Ok && bus.value == 0x8F);
    CHECK(setDacGainDbVerified(bus, 0x18, 0) == DacGainResult::Ok && bus.value == 0xBF);
    CHECK(setDacGainDbVerified(bus, 0x18, -95) == DacGainResult::Ok && bus.value == 0x01);
    for (int gain : {-96, 1, 100, (std::numeric_limits<int>::min)(), (std::numeric_limits<int>::max)()}) {
        const auto writes = bus.writes, reads = bus.reads;
        CHECK(setDacGainDbVerified(bus, 0x18, gain) == DacGainResult::InvalidArgument);
        CHECK(bus.writes == writes && bus.reads == reads);
    }
    bus = {}; bus.writeOkay = false;
    CHECK(setDacGainDbVerified(bus, 0x18, PlayerDacGainDb) == DacGainResult::WriteFailed);
    CHECK(bus.writes == 1 && bus.reads == 0);
    bus = {}; bus.readOkay = false;
    // Even an apparently matching byte cannot turn a failed bus transaction into success.
    CHECK(setDacGainDbVerified(bus, 0x18, PlayerDacGainDb) == DacGainResult::ReadFailed);
    bus = {}; bus.mismatch = true;
    CHECK(setDacGainDbVerified(bus, 0x18, PlayerDacGainDb) == DacGainResult::Mismatch);
    // Model of documented values, not a hardware power measurement:
    // two 0.9Vrms codec legs, 240k/100k amplifier gain, nominal 8-ohm speaker.
    const double voltage = 1.8 * std::pow(10.0, PlayerDacGainDb / 20.0) * 2.4;
    const double sineWatts = voltage * voltage / 8.0;
    CHECK(sineWatts > 0.14 && sineWatts < 0.15);
    CHECK(2.0 * sineWatts < 0.30); // Full-scale square-wave RMS bound in the same ideal model.
    std::printf("DAC gain: %u checks passed; fixed %d dB, REG32=0xA7, model %.4f W/8ohm sine\n",
                checks, PlayerDacGainDb, sineWatts);
}
