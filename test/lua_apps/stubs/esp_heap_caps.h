#pragma once
#include <cstddef>

constexpr unsigned MALLOC_CAP_SPIRAM = 1;
constexpr unsigned MALLOC_CAP_8BIT = 2;
void* heap_caps_malloc(std::size_t size, unsigned capabilities);
void heap_caps_free(void* pointer);
