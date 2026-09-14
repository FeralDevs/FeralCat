#pragma once
// Host compatibility for the unchanged Helix codec math. No Arduino device
// driver is simulated here; only allocation, ROM reads and compiler builtins.
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <climits>
#define PROGMEM
#define MALLOC_CAP_DEFAULT 0
#define MALLOC_CAP_INTERNAL 0
#define MALLOC_CAP_SPIRAM 0
#define pgm_read_word(address) (*reinterpret_cast<const uint16_t*>(address))
#define pgm_read_byte(address) (*reinterpret_cast<const uint8_t*>(address))
#define log_e(...) ((void)0)
#define log_i(...) ((void)0)
namespace helixhost {
void* allocate(std::size_t size);
void release(void* block);
#ifdef _MSC_VER
inline int leadingZeros(unsigned value)
{
    if (!value) return 32;
    int count = 0;
    for (unsigned bit = 0x80000000u; !(value & bit); bit >>= 1) ++count;
    return count;
}
#endif
}
#define heap_caps_malloc_prefer(size, ...) helixhost::allocate(size)
#define free(block) helixhost::release(block)
#ifdef _MSC_VER
#define __builtin_abs(value) std::abs(value)
#define __builtin_clz(value) helixhost::leadingZeros(value)
#endif
