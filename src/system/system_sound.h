#pragma once
#include <stdbool.h>
#include <stdint.h>
class DEVICES;
bool system_sound_init(DEVICES* dev);
void system_sound_set_volume(int percent);
void system_sound_set_key_enabled(bool enabled);
void system_sound_play_boot(uint32_t max_duration_ms);
void system_sound_play_button(void);
void system_sound_stop(void);
/* Hand the shared ES8311/I2S to another user (MeowPlayer, VU Meter, Tracker
 * beep, NES). Nests: the codec is reclaimed when the last suspend resumes.
 * suspend returns true only once the codec is actually released; on false
 * the caller must not touch the codec and must not call resume. resume
 * returns false if the codec could not be re-begun right now; system sound
 * then retries on the next tone. */
bool system_sound_suspend(void);
bool system_sound_resume(void);
