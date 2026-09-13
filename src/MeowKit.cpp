/**
 * @file MeowKit.cpp
 * @brief MeowKit application entry — manages device lifecycle
 */
#include "MeowKit.h"

bool MeowKit::Setup()
{
    _device = std::make_unique<DEVICES>();
    if (!_device) {
        printf("[MeowKit] BSP create failed\n");
        return false;
    }

    _device->init();   /* splash is drawn from Launcher::onCreate (tied to boot stages) */

    _launcher = std::make_unique<Launcher>(_device.get());
    _launcher->onCreate();

    return true;
}

void MeowKit::Loop()
{
    if (_launcher) {
        _launcher->onLoop();
    }
}