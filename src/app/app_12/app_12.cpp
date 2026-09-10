/**
 * @file  app_12.cpp
 * @brief Flash Mode app — see app_12.h.
 */
#include "app_12.h"
#include <Arduino.h>
#include "esp_system.h"
#include "esp32-hal-tinyusb.h"     /* usb_persist_restart / restart_type_t */
#include "../app_common/mk_tui.h"

namespace MOONCAKE::APPS
{

App12::App12(DEVICES* device) : _device(device)
{
    setAppInfo().name = "FlashMode";
}

void App12::_drawConfirm()
{
    LGFX_Class& lcd = _device->Lcd;
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "Flash Mode");

    lcd.setFont(&fonts::efontCN_16);
    int y = MK_LAYOUT::CONTENT_Y + 14;
    auto line = [&](const char* s, uint32_t c) {
        lcd.setTextColor(c, (uint32_t)MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::PAD, y);
        lcd.printf("%s", s);
        y += 22;
    };
    line("Reboot into USB download mode?", MK_PAL::TEXT_PRI);
    y += 4;
    line("For flashing new firmware from", MK_PAL::TEXT_SEC);
    line("the browser - no BOOT button.", MK_PAL::TEXT_SEC);
    y += 4;
    line("The screen goes dark; that is", MK_PAL::TEXT_SEC);
    line("normal. Power-cycle to return.", MK_PAL::TEXT_SEC);

    MK_TUI::drawFooter(lcd, "Enter", "Cancel");
}

void App12::_enterDownloadMode()
{
    /* Show a brief acknowledgement, then force the next boot into the ROM
     * serial-download mode and reset. The RTC flag survives the software reset;
     * a power cycle clears it and boots the firmware normally again. */
    LGFX_Class& lcd = _device->Lcd;
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "Flash Mode");
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor((uint32_t)MK_PAL::ACCENT, (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, 110);
    lcd.printf("Entering download mode...");
    delay(600);

    /* arduino-esp32's native-USB-aware reboot into the ROM serial bootloader:
     * it sets the download flag AND releases the USB PHY so the ROM can present
     * the download port (a plain esp_restart keeps USB in app mode = no port). */
    usb_persist_restart(RESTART_BOOTLOADER);
    /* Fallback if the above returns (older cores): force-download via RTC + reset. */
    esp_restart();
}

void App12::onOpen()
{
    _drawn = false;
}

void App12::onRunning()
{
    _device->button.update();
    _device->button.tick();

    if (!_drawn) { _drawConfirm(); _drawn = true; }

    /* A confirms (irreversible-ish reboot to download mode); hold B exits. */
    if (_device->button.A.pressed()) {
        _enterDownloadMode();          /* does not return */
    }

    delay(20);
}

void App12::onClose()
{
    /* Launcher repaints the menu on exit. */
}

} // namespace MOONCAKE::APPS
