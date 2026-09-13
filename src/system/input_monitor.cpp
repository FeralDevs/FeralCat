/**
 * @file input_monitor.cpp
 * @brief Live input state implementation (see input_monitor.h).
 */
#include "input_monitor.h"
#include <Arduino.h>
#include "../bsp/config.h"

struct Entry { const char* label; uint8_t gpio; bool probe; };

/* Known buttons (already INPUT_PULLUP via Button_Device) + candidate pins for
 * the unmapped joystick centre. Candidates are safe to read as INPUT_PULLUP at
 * runtime; if the centre isn't among them, more can be added here. */
static const Entry ENTRIES[] = {
    { "A",      HAL_PIN_BTN_A,     false },
    { "B",      HAL_PIN_BTN_B,     false },
    { "Up",     HAL_PIN_JOY_UP,    false },
    { "Down",   HAL_PIN_JOY_DOWN,  false },
    { "Left",   HAL_PIN_JOY_LEFT,  false },
    { "Right",  HAL_PIN_JOY_RIGHT, false },
    { "GPIO0",  0,                 true  },
    { "GPIO44", 44,                true  },   /* U0RXD — probing detaches serial RX */
    { "GPIO45", 45,                true  },
    { "GPIO46", 46,                true  },
};
static const int N = (int)(sizeof(ENTRIES) / sizeof(ENTRIES[0]));
static bool s_begun = false;

void input_monitor_begin(void)
{
    if (s_begun) return;
    s_begun = true;
    for (int i = 0; i < N; i++)
        if (ENTRIES[i].probe) pinMode(ENTRIES[i].gpio, INPUT_PULLUP);
}

int         input_monitor_count(void)      { return N; }
const char* input_monitor_label(int i)     { return (i >= 0 && i < N) ? ENTRIES[i].label : ""; }
int         input_monitor_gpio(int i)      { return (i >= 0 && i < N) ? ENTRIES[i].gpio : -1; }

bool input_monitor_active(int i)
{
    if (i < 0 || i >= N) return false;
    return digitalRead(ENTRIES[i].gpio) == LOW;   /* active-low */
}
