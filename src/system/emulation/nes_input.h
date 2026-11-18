#pragma once
#include <cstdint>

namespace meow::nes {
// Dedicated held-state input: launcher button edges/100 ms lockout are unsuitable
// for games. All six GPIOs are sampled together; wrap-safe 5 ms debounce.
class Input {
public:
    uint8_t update(uint8_t raw, uint32_t now) {
        for (unsigned i = 0; i < 8; ++i) {
            const uint8_t bit = uint8_t(1u << i);
            if ((raw ^ candidate_) & bit) {
                candidate_ ^= bit;
                changed_[i] = now;
            }
            if (uint32_t(now - changed_[i]) >= 5)
                held_ = uint8_t((held_ & ~bit) | (candidate_ & bit));
        }
        return clean(held_);
    }
    void reset() { candidate_ = held_ = 0; for (auto& t : changed_) t = 0; }
    static uint8_t clean(uint8_t pad) {
        if ((pad & 0x30) == 0x30) pad &= ~0x30;
        if ((pad & 0xc0) == 0xc0) pad &= ~0xc0;
        return pad;
    }
private:
    uint8_t candidate_ = 0, held_ = 0;
    uint32_t changed_[8]{};
};
}
