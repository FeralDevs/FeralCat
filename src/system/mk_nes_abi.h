#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// The SD app owns menus. Firmware owns the emulation session and peripherals.
// POD-only ABI; no callbacks or app-owned pointers survive any call.
#define MK_NES_ABI_VERSION 0x00010000u
#define MK_NES_ROOT "/roms/nes"
#define MK_NES_PATH_BYTES 256
enum { MK_NES_OK=0, MK_NES_PAUSED=1, MK_NES_SLEEP_REQUESTED=2,
       MK_NES_ERROR=-1, MK_NES_BUSY=-2, MK_NES_INVALID=-3 };
enum { MK_NES_SAVE=0, MK_NES_RESET=1, MK_NES_SOUND=2,
       MK_NES_CLOSE=3, MK_NES_SLEEP=4 };
enum { MK_NES_DIRECTORY=1, MK_NES_HEADER_VALID=2, MK_NES_SUPPORTED=4,
       MK_NES_EXPERIMENTAL=8, MK_NES_BATTERY=16 };
enum { MK_NES_LIST_TRUNCATED=1, MK_NES_LIST_SKIPPED=2 };
// Button bits deliberately match the NES pad: A/B/Select/Start/Up/Down/Left/Right.
enum { MK_NES_A=1, MK_NES_B=2, MK_NES_SELECT=4, MK_NES_START=8,
       MK_NES_UP=16, MK_NES_DOWN=32, MK_NES_LEFT=64, MK_NES_RIGHT=128 };
typedef struct {
    uint32_t count, flags;
    char path[MK_NES_PATH_BYTES];
} mk_nes_catalog_t;
typedef struct {
    char name[MK_NES_PATH_BYTES];
    uint32_t bytes, flags, mapper, prg_bytes, chr_bytes, region; // region=50 or60
} mk_nes_entry_t;
typedef struct {
    uint32_t loaded, sound, mapper, region, fps, sram_bytes, flags;
    char rom[MK_NES_PATH_BYTES];
} mk_nes_state_t;
typedef struct {
    uint32_t held, pressed, touching, touch_pressed;
    int32_t touch_x, touch_y;
    uint32_t sleep_requested;
} mk_nes_input_t;
uint32_t mk_nes_version(void);
int mk_nes_begin(void); // 0 on success, <0 on error; call end after any attempt.
void mk_nes_end(void); // Idempotent; last-chance save and full resource cleanup.
// Absolute bounded paths under MK_NES_ROOT; no dot segments or backslashes.
int mk_nes_catalog_open(const char* path); // count >=0, error <0; no ROM may be open.
void mk_nes_catalog_info(mk_nes_catalog_t* out);
int mk_nes_catalog_entry(uint32_t index, mk_nes_entry_t* out); // 0 success
int mk_nes_open(const char* path); // Loads paused. 0 success, negative error.
// Blocks only during gameplay, returns PAUSED or SLEEP_REQUESTED with ROM retained.
// On error returns negative, stopped, with diagnostic available. No menu callback.
int mk_nes_run(void);
// CLOSE saves first and keeps ROM open if that save fails. END always cleans up.
int mk_nes_command(int command, int value);
void mk_nes_state(mk_nes_state_t* out);
void mk_nes_error(char* out, uint32_t bytes);
void mk_nes_poll(mk_nes_input_t* out); // also polls battery/settings; do not mix mk_input_poll
void mk_nes_wait_release(void);
#ifdef __cplusplus
}
#endif
