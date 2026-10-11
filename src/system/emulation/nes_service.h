#pragma once
class DEVICES;
// Bind the firmware-owned service before native app execution.
void nes_service_attach(DEVICES* device);
// Called by the existing shutdown hook on the session task before power is cut.
void nes_service_prepare_shutdown();
bool nes_service_active();
