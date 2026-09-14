/**
 * @file berry_engine.cpp
 * @brief Embedded Berry scripting engine implementation.
 *
 * Exposes a small global API to scripts:
 *   print(...)                 -> on-screen console (+ Serial)
 *   delay(ms) / millis()
 *   led(r, g, b)               -> onboard WS2812 (single pixel)
 *   pin_mode(n, out) / pin_write(n, v) / pin_read(n)
 *   button("A"|"B"|"up"|...)   -> bool (currently pressed)
 *
 * Safety: a line hook aborts the script if it runs past a time limit or the
 * user holds B — so an infinite loop can't hang the device.
 */
#include "berry_engine.h"
#include <Arduino.h>
#include <cstring>
#include "berry.h"
#include "../bsp/config.h"

static DEVICES*     s_dev = nullptr;
static berry_out_fn s_out = nullptr;
static uint32_t     s_deadline = 0;

void berry_engine_attach(DEVICES* dev) { s_dev = dev; }
void berry_set_output(berry_out_fn fn) { s_out = fn; }

/* Berry routes all print() output through be_writebuffer (removed from
 * be_port.c) — send it to the console sink and the serial log. */
extern "C" void be_writebuffer(const char* buffer, size_t length)
{
    if (s_out) s_out(buffer, (int)length);
    Serial.write((const uint8_t*)buffer, length);
}

/* ── device API ─────────────────────────────────────────────────────────── */

static int m_delay(bvm* vm)
{
    if (be_top(vm) >= 1 && be_isint(vm, 1)) delay((uint32_t)be_toint(vm, 1));
    be_return_nil(vm);
}

static int m_millis(bvm* vm) { be_pushint(vm, (bint)millis()); be_return(vm); }

static int m_led(bvm* vm)
{
    if (s_dev && be_top(vm) >= 3)
        s_dev->led.setColor((uint8_t)be_toint(vm, 1),
                            (uint8_t)be_toint(vm, 2),
                            (uint8_t)be_toint(vm, 3));
    be_return_nil(vm);
}

static int m_pin_mode(bvm* vm)
{
    if (be_top(vm) >= 2)
        pinMode((uint8_t)be_toint(vm, 1), be_toint(vm, 2) ? OUTPUT : INPUT_PULLUP);
    be_return_nil(vm);
}

static int m_pin_write(bvm* vm)
{
    if (be_top(vm) >= 2)
        digitalWrite((uint8_t)be_toint(vm, 1), be_toint(vm, 2) ? HIGH : LOW);
    be_return_nil(vm);
}

static int m_pin_read(bvm* vm)
{
    int v = (be_top(vm) >= 1) ? digitalRead((uint8_t)be_toint(vm, 1)) : 0;
    be_pushint(vm, v);
    be_return(vm);
}

static int m_button(bvm* vm)
{
    bool pressed = false;
    if (s_dev && be_top(vm) >= 1 && be_isstring(vm, 1)) {
        const char* n = be_tostring(vm, 1);
        s_dev->button.update();
        Button_Class* b = nullptr;
        if      (!strcmp(n, "A"))     b = &s_dev->button.A;
        else if (!strcmp(n, "B"))     b = &s_dev->button.B;
        else if (!strcmp(n, "up"))    b = &s_dev->button.Up;
        else if (!strcmp(n, "down"))  b = &s_dev->button.Down;
        else if (!strcmp(n, "left"))  b = &s_dev->button.Left;
        else if (!strcmp(n, "right")) b = &s_dev->button.Right;
        if (b) pressed = (b->state() == Button_Class::PRESSED);
    }
    be_pushbool(vm, pressed);
    be_return(vm);
}

/* Line hook: abort on timeout or a held B button. */
static int m_hook(bvm* vm)
{
    if (millis() > s_deadline)              be_raise(vm, "stopped", "time limit reached");
    if (digitalRead(HAL_PIN_BTN_B) == LOW)  be_raise(vm, "stopped", "stopped by user (B)");
    be_return_nil(vm);
}

static void register_api(bvm* vm)
{
    be_regfunc(vm, "delay",     m_delay);
    be_regfunc(vm, "millis",    m_millis);
    be_regfunc(vm, "led",       m_led);
    be_regfunc(vm, "pin_mode",  m_pin_mode);
    be_regfunc(vm, "pin_write", m_pin_write);
    be_regfunc(vm, "pin_read",  m_pin_read);
    be_regfunc(vm, "button",    m_button);
}

int berry_run(const char* code, uint32_t timeout_ms, char* err, int err_len)
{
    if (err && err_len) err[0] = '\0';
    bvm* vm = be_vm_new();
    if (!vm) { if (err && err_len) snprintf(err, err_len, "VM alloc failed"); return -1; }

    register_api(vm);

    s_deadline = millis() + timeout_ms;
    be_pushntvfunction(vm, m_hook);   /* install the safety line hook */
    be_sethook(vm, "l");
    be_pop(vm, 1);

    int rc = be_loadstring(vm, code);
    if (rc == 0) rc = be_pcall(vm, 0);
    if (rc != 0 && err && err_len) {
        const char* msg = be_tostring(vm, -1);
        snprintf(err, err_len, "%s", msg ? msg : "error");
    }

    be_vm_delete(vm);
    return rc;
}
