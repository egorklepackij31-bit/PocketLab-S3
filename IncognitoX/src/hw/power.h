#pragma once

#include <Arduino.h>

namespace power {
void    begin();
void    tick();
bool    available();
float   voltage();
int     percent();
bool    chargingGuess();
float   chipTempC();
}
