#pragma once

#include <Arduino.h>

namespace buzzer {
void begin();
void enable(bool on);
bool enabled();
void setVolume(uint8_t percent);

void beep(uint16_t freq = 2400, uint16_t ms = 25);
void click();
void ok();
void err();
void start();

enum Melody : uint8_t { MEL_START, MEL_ERROR, MEL_ALARM, MEL_COUNT };
void play(uint8_t melody);

void tick();
void stop();
}
