#pragma once

#include <Arduino.h>

#include "lib/nmea.h"

namespace gps {
void          begin();
void          tick();
void          setPaused(bool paused);
bool          present();
bool          hasFix();
const GpsFix &fix();
uint32_t      sentences();
uint32_t      lastByteMs();
void          syncClock(int8_t tzHours);
String        utcTimeString();
}
