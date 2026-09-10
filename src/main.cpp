#include "MeowKit.h"
#include <Arduino.h>
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

MeowKit meowkit;

void setup(){
    /* ── Boot hardening (crash-reset-loop guard) ──────────────────────────
     * On some retail units the early I2C/AXP173 power bring-up triggers a
     * reset loop. Two known mechanisms, disabled here before any peripheral
     * is touched:
     *   1. Brownout detector firing on the current spike during init.
     *   2. Task/interrupt watchdog resetting during a slow I2C probe.
     * This does not fix a genuine code fault, but stops a marginal-power or
     * slow-probe boot from crash-looping. */
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);   /* disable brownout reset */
    disableCore0WDT();
    disableLoopWDT();

    meowkit.Setup();
}

void loop() { meowkit.Loop();  }
