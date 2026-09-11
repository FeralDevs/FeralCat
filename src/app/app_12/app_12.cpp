/**
 * @file  app_12.cpp
 * @brief Firmware app — SD‑card OTA update + USB download mode. See app_12.h.
 */
#include "app_12.h"
#include <Arduino.h>
#include <Update.h>
#include <SD_MMC.h>
#include "esp_system.h"
#include "esp32-hal-tinyusb.h"     /* usb_persist_restart / restart_type_t */
#include "../app_common/mk_tui.h"

namespace MOONCAKE::APPS
{

App12::App12(DEVICES* device) : _device(device)
{
    setAppInfo().name = "Firmware";
}

void App12::_drawMenu()
{
    LGFX_Class& lcd = _device->Lcd;
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "Firmware");
    MK_TUI::drawMenuItem(lcd, 0, "Update from SD",   "firmware.bin", _sel == 0);
    MK_TUI::drawMenuItem(lcd, 1, "USB Download Mode", "flash via PC", _sel == 1);
    MK_TUI::drawFooter(lcd, "Select", "Exit");
}

/* Blocking modal: show a message, wait for A/B, then return to the menu. */
void App12::_modal(const char* line1, const char* line2, uint32_t color)
{
    LGFX_Class& lcd = _device->Lcd;
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "Firmware");
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor(color, (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, 100);
    lcd.printf("%s", line1);
    if (line2 && line2[0]) {
        lcd.setTextColor((uint32_t)MK_PAL::TEXT_SEC, (uint32_t)MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::PAD, 122);
        lcd.printf("%s", line2);
    }
    MK_TUI::drawFooter(lcd, "OK", "");
    while (true) {
        _device->button.update();
        _device->button.tick();
        if (_device->button.A.pressed() || _device->button.B.pressed()) break;
        delay(20);
    }
    _dirty = true;
}

void App12::_usbDownload()
{
    LGFX_Class& lcd = _device->Lcd;
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "Firmware");
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor((uint32_t)MK_PAL::ACCENT, (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, 110);
    lcd.printf("Entering download mode...");
    delay(600);
    usb_persist_restart(RESTART_BOOTLOADER);   /* does not return */
    esp_restart();
}

void App12::_sdUpdate()
{
    LGFX_Class& lcd = _device->Lcd;

    _device->sd.begin();
    if (!_device->sd.isReady()) { _modal("No SD card", "Insert a card and retry", MK_PAL::ERR); return; }

    File f = SD_MMC.open("/firmware.bin", FILE_READ);
    if (!f || f.isDirectory()) { if (f) f.close();
        _modal("firmware.bin not found", "Copy it to the SD root", MK_PAL::ERR); return; }

    size_t sz = f.size();
    if (sz < 0x10000) { f.close(); _modal("Image too small", "Not a valid firmware.bin", MK_PAL::ERR); return; }
    if (!Update.begin(sz)) { f.close(); _modal("Update init failed", Update.errorString(), MK_PAL::ERR); return; }

    /* Static progress screen (only the bar/percent update each step). */
    MK_TUI::clearScreen(lcd);
    MK_TUI::drawHeader(lcd, "SD Update");
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor((uint32_t)MK_PAL::TEXT_PRI, (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, MK_LAYOUT::CONTENT_Y + 16);
    lcd.printf("Writing firmware...");
    lcd.setTextColor((uint32_t)MK_PAL::ERR, (uint32_t)MK_PAL::BLACK);
    lcd.setCursor(MK_LAYOUT::PAD, MK_LAYOUT::CONTENT_Y + 38);
    lcd.printf("Do NOT power off.");

    static uint8_t buf[4096];
    size_t done = 0;
    int last = -1;
    while (f.available()) {
        int n = f.read(buf, sizeof(buf));
        if (n <= 0) break;
        if ((int)Update.write(buf, n) != n) {
            f.close(); Update.abort();
            _modal("Write error", Update.errorString(), MK_PAL::ERR);
            return;
        }
        done += n;
        int pct = (int)(done * 100 / sz);
        if (pct != last) {
            last = pct;
            MK_TUI::drawProgress(lcd, MK_LAYOUT::CONTENT_Y + 80, pct, nullptr);
            lcd.setFont(&fonts::efontCN_16);
            lcd.setTextColor((uint32_t)MK_PAL::ACCENT, (uint32_t)MK_PAL::BLACK);
            lcd.setCursor(MK_LAYOUT::W - 60, MK_LAYOUT::CONTENT_Y + 78);
            lcd.printf("%3d%%", pct);
        }
    }
    f.close();

    if (Update.end(true)) { _modal("Update complete", "Rebooting...", MK_PAL::OK); delay(300); esp_restart(); }
    else                  { _modal("Update failed", Update.errorString(), MK_PAL::ERR); }
}

void App12::onOpen()
{
    _sel = 0;
    _dirty = true;
}

void App12::onRunning()
{
    _device->button.update();
    _device->button.tick();

    if (_device->button.Up.pressed())   { _sel = (_sel + ROWS - 1) % ROWS; _dirty = true; }
    if (_device->button.Down.pressed()) { _sel = (_sel + 1) % ROWS; _dirty = true; }
    if (_device->button.A.pressed()) {
        if (_sel == 0) _sdUpdate(); else _usbDownload();
    }

    if (_dirty) { _drawMenu(); _dirty = false; }

    delay(20);
}

void App12::onClose() {}

} // namespace MOONCAKE::APPS
