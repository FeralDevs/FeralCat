#pragma once
#include "nes/nes.h"
extern mapintf_t map0_intf;
extern mapintf_t map1_intf;
extern mapintf_t map2_intf;
extern mapintf_t map3_intf;
extern mapintf_t map4_intf;
extern mapintf_t map5_intf;
extern mapintf_t map7_intf;
extern mapintf_t map9_intf;
extern mapintf_t map10_intf;
extern mapintf_t map11_intf;
extern mapintf_t map23_intf;
extern mapintf_t map24_intf;
extern mapintf_t map66_intf;
static const mapintf_t *mappers[] = {
    &map0_intf,
    &map1_intf,
    &map2_intf,
    &map3_intf,
    &map4_intf,
    &map5_intf,
    &map7_intf,
    &map9_intf,
    &map10_intf,
    &map11_intf,
    &map23_intf,
    &map24_intf,
    &map66_intf,
    NULL
};
