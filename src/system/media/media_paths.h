#pragma once
#include <cstddef>
namespace meow::media {
bool validMediaText(const char* text, std::size_t length);
bool normalizeMediaPath(const char* baseDirectory, const char* text, std::size_t length,
                        char* result, std::size_t capacity);
int compareMediaPaths(const char* left, const char* right);
}
