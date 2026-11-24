#include "hw/selftest.h"

#include <stdarg.h>

#include <esp_heap_caps.h>

#include "config.h"
#include "hw/display.h"
#include "hw/gps.h"
#include "hw/power.h"
#include "hw/storage.h"
#include "lib/cc1101.h"
#include "lib/nrf24.h"
#include "lib/pn532.h"
#include "lib/rc522.h"
#include "pins.h"

namespace selftest {
namespace {
Item     s_items[12];
int      s_count = 0;
uint32_t s_last = 0;

void add(const char *name, Status st, const char *fmt, ...) {
    if (s_count >= 12) return;
    Item &it = s_items[s_count++];
    it.name = name;
    it.status = st;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(it.detail, sizeof(it.detail), fmt, ap);
    va_end(ap);
}
}

int run() {
    s_count = 0;
    s_last = millis();

    add("Display", ST_OK, "%dx%d ST7789", display::width(), display::height());

    if (storage::mounted())
        add("microSD", ST_OK, "%s, %lu MB", storage::cardType().c_str(),
            (unsigned long)(storage::totalBytes() / (1024ULL * 1024ULL)));
    else
        add("microSD", ST_FAIL, "not found (CS=%d)", PIN_SD_CS);

    if (cc1101.present())
        add("CC1101", ST_OK, "ver %02X, %.2f MHz", cc1101.version(),
            cc1101.frequency());
    else
        add("CC1101", ST_FAIL, "no answer (CS=%d)", PIN_CC_CS);

    if (nrf24.present())
        add("NRF24L01", ST_OK, "ch %d, RPD ok", nrf24.channel());
    else
        add("NRF24L01", ST_FAIL, "no answer (CS=%d)", PIN_NRF_CS);

    if (rc522.present())
        add("RC522", ST_OK, "ver %02X", rc522.version());
    else
        add("RC522", ST_FAIL, "no answer (CS=%d)", PIN_RC522_CS);

    if (pn532.present())
        add("PN532", ST_OK, "fw %06lX (I2C 0x24)",
            (unsigned long)(pn532.firmware() >> 8));
    else
        add("PN532", ST_FAIL, "no answer (I2C 0x24)");

    if (gps::sentences() > 0)
        add("GPS NEO", ST_OK, "%lu sentences", (unsigned long)gps::sentences());
    else
        add("GPS NEO", ST_WARN, "no NMEA yet (RX=%d)", PIN_GPS_RX);

    pinMode(PIN_IR_RX, INPUT_PULLUP);
    add("IR receiver", digitalRead(PIN_IR_RX) ? ST_OK : ST_WARN,
        digitalRead(PIN_IR_RX) ? "idle high" : "line low?");

    if (power::percent() >= 0)
        add("Battery", ST_OK, "%.2f V, %d%%", power::voltage(), power::percent());
    else
        add("Battery", ST_WARN, "no divider (GPIO%d)", PIN_BATTERY_ADC);

    const uint8_t btns[5] = {PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_LEFT, PIN_BTN_RIGHT,
                             PIN_BTN_OK};
    int stuck = 0;
    for (int i = 0; i < 5; i++) {
        pinMode(btns[i], INPUT_PULLUP);
        if (digitalRead(btns[i]) == LOW) stuck++;
    }
    add("Buttons", stuck ? ST_WARN : ST_OK, stuck ? "%d stuck?" : "5 keys idle",
        stuck);

    size_t heap = ESP.getFreeHeap();
    add("Memory", heap > 60000 ? ST_OK : ST_WARN, "heap %lu KB, PSRAM %s",
        (unsigned long)(heap / 1024),
#if IX_USE_PSRAM
        "on"
#else
        "off"
#endif
    );

    return s_count;
}

int count() { return s_count; }

const Item &at(int i) {
    static Item empty = {"?", ST_WARN, ""};
    if (i < 0 || i >= s_count) return empty;
    return s_items[i];
}

int failures() {
    int n = 0;
    for (int i = 0; i < s_count; i++)
        if (s_items[i].status == ST_FAIL) n++;
    return n;
}

uint32_t lastRunMs() { return s_last; }

}
