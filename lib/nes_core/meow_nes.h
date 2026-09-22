#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Singleton, one task only. ROM is borrowed until close and never modified.
 * Returned frame pointers survive until the next step/reset/close. */
#define MEOW_NES_API_VERSION 1
#define MEOW_NES_MAX_ROM_BYTES (2u * 1024u * 1024u)
enum { MEOW_NES_A=1, MEOW_NES_B=2, MEOW_NES_SELECT=4, MEOW_NES_START=8,
       MEOW_NES_UP=16, MEOW_NES_DOWN=32, MEOW_NES_LEFT=64, MEOW_NES_RIGHT=128 };
typedef enum { MEOW_NES_AUTO=0, MEOW_NES_NTSC=1, MEOW_NES_PAL=2 } meow_nes_region_t;
typedef enum { MEOW_NES_OK=0, MEOW_NES_BAD_ARGUMENT=-1, MEOW_NES_BAD_ROM=-2,
    MEOW_NES_UNSUPPORTED=-3, MEOW_NES_NO_MEMORY=-4, MEOW_NES_NOT_OPEN=-5,
    MEOW_NES_BUSY=-6 } meow_nes_result_t;
typedef void *(*meow_nes_alloc_fn)(size_t bytes, void *user);
typedef void (*meow_nes_free_fn)(void *ptr, void *user);
typedef void (*meow_nes_log_fn)(int level, const char *message, void *user);
typedef struct {
    uint32_t sample_rate; /* 0 => 44100; supported: 44100, 48000. Mono S16. */
    meow_nes_region_t region;
    meow_nes_alloc_fn alloc; /* Pair or both NULL. Used for all owned buffers. */
    meow_nes_free_fn free;
    meow_nes_log_fn log;
    void *user;
} meow_nes_options_t;
typedef struct {
    uint16_t mapper;
    meow_nes_region_t region;
    uint32_t sample_rate;
    uint32_t frames_per_second; /* Core uses integer 60/50. */
    size_t sram_bytes;
    uint32_t rom_crc32; /* PRG+CHR, excludes header/trainer/trailing bytes. */
    bool battery;
    bool experimental_mapper;
} meow_nes_info_t;
typedef struct {
    const uint8_t *pixels; /* First visible pixel; use pitch for next row. */
    const uint16_t *palette_rgb565; /* Host-endian RGB565, 256 entries. */
    uint16_t width, height, pitch;
    const int16_t *pcm;
    size_t pcm_samples;
    uint64_t frame_number;
} meow_nes_frame_t;
meow_nes_result_t meow_nes_open(const uint8_t *rom, size_t bytes, const meow_nes_options_t *options);
meow_nes_result_t meow_nes_step(uint8_t held_pad, meow_nes_frame_t *frame);
void meow_nes_close(void); /* Idempotent, including after failed open. */
meow_nes_result_t meow_nes_reset(bool hard); /* Preserves Battery-RAM. */
const meow_nes_info_t *meow_nes_info(void); /* NULL if closed. */
size_t meow_nes_sram_size(void); /* Battery-RAM only. */
meow_nes_result_t meow_nes_sram_read(void *dst, size_t bytes);
meow_nes_result_t meow_nes_sram_write(const void *src, size_t bytes);
bool meow_nes_mapper_supported(uint16_t mapper);
const char *meow_nes_error_string(meow_nes_result_t result);
uint32_t meow_nes_crc32(uint32_t seed, const void *data, size_t bytes);
#ifdef __cplusplus
}
#endif
