/**
 * @file  app_20.cpp
 * @brief ELF Test — see app_20.h.
 */
#include "app_20.h"
#include <Arduino.h>
#include "../app_common/mk_tui.h"
#include "../../system/elf_runner.h"

namespace MOONCAKE::APPS
{

#define ELF_TEST_PATH "/apps/hello/app.elf"

App20::App20(DEVICES* device) : _device(device)
{
    setAppInfo().name = "ELF Test";
}

void App20::onOpen()
{
    _device->sd.begin();
    _haveCanvas = false;
    _canvas = new lgfx::LGFX_Sprite(&_device->Lcd);
    if (_canvas) {
        _canvas->setColorDepth(16);
        _canvas->setPsram(true);
        _haveCanvas = _canvas->createSprite(MK_LAYOUT::W, MK_LAYOUT::H);
    }
    strncpy(_status, "Press A to run app.elf", sizeof(_status) - 1);
    _dirty = true;
}

void App20::_present()
{
    LGFX_Class& dst = _device->Lcd;
    auto draw = [&](auto& lcd) {
        MK_TUI::clearScreen(lcd);
        MK_TUI::drawHeader(lcd, "ELF Test");
        lcd.setFont(&fonts::efontCN_16);
        lcd.setTextColor((uint32_t)MK_PAL::TEXT_SEC, (uint32_t)MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::PAD, MK_LAYOUT::CONTENT_Y + 12);
        lcd.printf("%s", ELF_TEST_PATH);
        lcd.setTextColor((uint32_t)MK_PAL::WHITE, (uint32_t)MK_PAL::BLACK);
        lcd.setCursor(MK_LAYOUT::PAD, MK_LAYOUT::CONTENT_Y + 44);
        lcd.printf("%s", _status);
        MK_TUI::drawFooter(lcd, "Run", "Exit");
    };
    if (_haveCanvas) { draw(*_canvas); _canvas->pushSprite(&dst, 0, 0); }
    else             draw(dst);
}

void App20::onRunning()
{
    _device->button.update();
    _device->button.tick();

    if (_device->button.A.pressed()) {
        strncpy(_status, "running...", sizeof(_status) - 1);
        _present();
        meow_elf_run_file(ELF_TEST_PATH, 0, nullptr, _status, sizeof(_status));
        _dirty = true;
    }

    if (_dirty) { _present(); _dirty = false; }
    delay(20);
}

void App20::onClose()
{
    if (_canvas) { _canvas->deleteSprite(); delete _canvas; _canvas = nullptr; }
    _haveCanvas = false;
}

} // namespace MOONCAKE::APPS
