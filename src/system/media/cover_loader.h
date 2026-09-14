#pragma once
#include <FS.h>
#include <cstddef>
#include <cstdint>

namespace meow::media {
constexpr std::size_t CoverWidth = 96, CoverHeight = 96;
constexpr std::size_t CoverPixels = CoverWidth * CoverHeight;
constexpr std::size_t MaxCoverBytes = 256 * 1024;
constexpr std::size_t MaxCoverDimension = 1024;
constexpr std::size_t MaxCoverTagBytes = 512 * 1024;
constexpr std::size_t MaxCoverFrames = 128;
constexpr std::uint32_t CoverBudgetMs = 500;
enum class CoverResult { Ready, Missing, Unsupported, Invalid, Cancelled, NoMemory, TimedOut };
enum class CoverSource { None, CoverJpeg, FolderJpeg, EmbeddedJpeg };
struct CoverInfo { CoverSource source = CoverSource::None; std::uint16_t width = 0, height = 0; };
struct CoverHooks {
    bool (*cancel)(void*) = nullptr;
    std::uint32_t (*clockMs)(void*) = nullptr; // Optional; defaults to the platform monotonic clock.
    void* context = nullptr;
};

// Worker only, after stopping playback and before opening the next audio stream.
// Caller owns a private/unpublished buffer of at least CoverPixels uint16_t values,
// preferably PSRAM. On Ready it contains a letterboxed, native RGB565 96x96 image.
// On any other result discard its contents. All file handles and temporary memory
// are closed/freed before returning. This module never publishes shared pointers.
CoverResult loadTrackCover(fs::FS& source, const char* canonicalTrackPath,
                          std::uint16_t* pixels, std::size_t capacity,
                          CoverInfo& info, CoverHooks hooks = {});
} // namespace meow::media
