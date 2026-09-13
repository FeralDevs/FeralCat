/**
 * @file input_monitor.h
 * @brief Live input state for the Settings > Debug > Input Monitor.
 *
 * Exposes the known buttons (A/B + joystick directions) plus a few candidate
 * GPIOs, so pressing an input — including the joystick centre, whose pin isn't
 * mapped yet — shows on screen which GPIO goes active. That's how we discover
 * the centre-press pin.
 */
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

/** Configure the candidate probe pins (INPUT_PULLUP). Safe to call repeatedly. */
void        input_monitor_begin(void);

int         input_monitor_count(void);
const char* input_monitor_label(int i);   /* e.g. "A", "Up", "GPIO0" */
int         input_monitor_gpio(int i);
bool        input_monitor_active(int i);   /* true = currently pressed (LOW) */

#ifdef __cplusplus
}
#endif
