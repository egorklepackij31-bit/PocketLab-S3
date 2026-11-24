#pragma once

#include <Arduino.h>

struct Settings {
    uint32_t magic       = 0x4B4F4D45;
    uint8_t  brightness  = 80;
    uint8_t  volume      = 50;
    uint8_t  sound       = 1;
    uint8_t  accent      = 0;
    int8_t   tz          = 3;
    uint8_t  autosleep   = 0;
    uint8_t  sdlog       = 1;
    uint8_t  splash      = 1;
    uint8_t  wheelInvert = 0;
};

extern Settings settings;

namespace cfg {
void        begin();
void        save();
void        reset();
void        apply();

uint8_t     accentCount();
const char *accentName(uint8_t idx);
uint16_t    accentColor(uint8_t idx);
}
