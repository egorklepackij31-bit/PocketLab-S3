#include "hw/gps.h"

#include <sys/time.h>
#include <time.h>

#include "config.h"
#include "pins.h"

namespace gps {
namespace {
NmeaParser s_parser;
uint32_t   s_lastByte = 0;
bool       s_started = false;
bool       s_paused = false;
HardwareSerial &s_uart = Serial2;
}

void begin() {
    s_uart.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    s_started = true;
}

void setPaused(bool paused) { s_paused = paused; }

void tick() {
    if (!s_started || s_paused) return;
    int budget = 256;
    while (s_uart.available() && budget-- > 0) {
        s_parser.feed((char)s_uart.read());
        s_lastByte = millis();
    }
}

bool present() { return s_parser.fix().sentences > 0; }
bool hasFix() { return s_parser.fix().valid && (millis() - s_lastByte) < 3000; }
const GpsFix &fix() { return s_parser.fix(); }
uint32_t sentences() { return s_parser.fix().sentences; }
uint32_t lastByteMs() { return s_lastByte; }

void syncClock(int8_t tzHours) {
    (void)tzHours;
    const GpsFix &f = s_parser.fix();
    if (!f.valid || f.year < 2020) return;
    setenv("TZ", "UTC0", 1);
    tzset();
    struct tm t = {};
    t.tm_year = f.year - 1900;
    t.tm_mon = f.month - 1;
    t.tm_mday = f.day;
    t.tm_hour = f.hour;
    t.tm_min = f.minute;
    t.tm_sec = f.second;
    time_t utc = mktime(&t);
    struct timeval tv = {utc, 0};
    settimeofday(&tv, nullptr);
}

String utcTimeString() {
    const GpsFix &f = s_parser.fix();
    char b[16];
    snprintf(b, sizeof(b), "%02d:%02d:%02d", f.hour, f.minute, f.second);
    return String(b);
}

}
