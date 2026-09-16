#pragma once
#include <cstdint>
#include <cstdlib>
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
extern int tracker_test_lock_depth;
extern void (*tracker_test_before_lock)();
inline void portENTER_CRITICAL(portMUX_TYPE*) {
    if (tracker_test_lock_depth == 0 && tracker_test_before_lock) tracker_test_before_lock();
    ++tracker_test_lock_depth;
}
inline void portEXIT_CRITICAL(portMUX_TYPE*) { if (--tracker_test_lock_depth < 0) std::abort(); }
uint32_t millis();
void delay(uint32_t ms);
