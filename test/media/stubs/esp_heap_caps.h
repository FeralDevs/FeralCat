#pragma once
#include <cstddef>
#define MALLOC_CAP_SPIRAM 1u
#define MALLOC_CAP_8BIT 2u
void* heap_caps_malloc(size_t size, unsigned caps);
void heap_caps_free(void* pointer);
namespace mock {
struct Heap {
    size_t calls = 0, live = 0, bytes = 0, peakBytes = 0, failCall = 0;
    bool wrongCapabilities = false;
};
extern Heap heap;
}
