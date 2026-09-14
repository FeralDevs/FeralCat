/* Sprite-only host adapter for LovyanGFX's SDL platform. No SDL window/panel
 * implementation is linked. Rendering uses the unchanged LGFX_Sprite core. */
#pragma once
#define SDL_h_
#include <chrono>
#include <cstdint>
#include <thread>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_mutex;
using SDL_Keymod = int;
using SDL_KeyCode = int;
constexpr SDL_KeyCode SDLK_UNKNOWN = 0;

inline uint64_t SDL_GetPerformanceCounter() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline uint64_t SDL_GetPerformanceFrequency() { return 1000000000ULL; }
inline uint32_t SDL_GetTicks() {
    return static_cast<uint32_t>(SDL_GetPerformanceCounter() / 1000000ULL);
}
inline void SDL_Delay(uint32_t milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}
