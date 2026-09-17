/**
 * @file  app_sdk.h
 * @brief MeowKit native-app SDK — firmware side of the mk_app ABI.
 *
 * Implements the mk_* functions in mk_app_abi.h on top of the real drivers and
 * MK_TUI, and registers them with the ELF loader's symbol table so a loaded
 * native app can call them. Call app_sdk_init() once at boot.
 */
#pragma once
#include "mk_app_abi.h"

#ifdef __cplusplus
class DEVICES;
/* Store the device and register the mk_* symbol table with the ELF loader.
 * Idempotent; call once after the display/drivers are up. */
void app_sdk_init(DEVICES* dev);

/* Power-button → sleep check, safe to call from any loop (returns true if it
 * slept). Called by mk_input_poll (ELF apps) and the launcher (home + apps). */
bool app_sleep_check(void);
#endif
