/**
 * @file berry_engine.h
 * @brief Embedded Berry scripting engine — run .be scripts from the SD card
 *        with a small device API (led / pin / button / delay / print).
 */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
#include "../bsp/devices.h"
void berry_engine_attach(DEVICES* dev);   /* C++ only: give the API device access */
extern "C" {
#endif

/** Output sink for script `print(...)` — the app routes it to a console. */
typedef void (*berry_out_fn)(const char* text, int len);
void berry_set_output(berry_out_fn fn);

/**
 * Run a Berry script from source. Blocks until it finishes, hits the timeout,
 * or the user holds B. On error, `err` gets the message. Returns 0 on success.
 */
int berry_run(const char* code, uint32_t timeout_ms, char* err, int err_len);

#ifdef __cplusplus
}
#endif
