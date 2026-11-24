#include "app/settings.h"

#include <Preferences.h>

#include "app/log.h"
#include "hw/buzzer.h"
#include "hw/display.h"
#include "hw/gfx.h"

Settings settings;

namespace cfg {
namespace {
Preferences prefs;
const char *kKey = "cfg1";

const char *kAccentNames[] = {"Оранжевый", "Янтарный", "Красный", "Зелёный", "Голубой", "Фиолетовый"};
const uint16_t kAccentColors[] = {0xFC80, 0xFD20, 0xF800, 0x2FE5,
                                  0x2D7F, 0x9A7F};
}

uint8_t accentCount() { return sizeof(kAccentColors) / sizeof(kAccentColors[0]); }
const char *accentName(uint8_t i) {
    return i < accentCount() ? kAccentNames[i] : "?";
}
uint16_t accentColor(uint8_t i) {
    return i < accentCount() ? kAccentColors[i] : 0xFC80;
}

void apply() {
    gfx::col::accent = accentColor(settings.accent);
    display::backlight(settings.brightness);
    buzzer::enable(settings.sound != 0);
    buzzer::setVolume(settings.volume);
    klog::setToSd(settings.sdlog != 0);
}

void begin() {
    prefs.begin("incognitox", false);
    Settings def;
    if (prefs.getBytesLength(kKey) == sizeof(Settings)) {
        prefs.getBytes(kKey, &settings, sizeof(Settings));
    }
    if (settings.magic != def.magic) {
        settings = def;
        save();
    }
    if (settings.brightness < 5) settings.brightness = 5;
    if (settings.brightness > 100) settings.brightness = 100;
    apply();
}

void save() {
    settings.magic = 0x4B4F4D45;
    prefs.putBytes(kKey, &settings, sizeof(Settings));
}

void reset() {
    settings = Settings();
    save();
    apply();
}

}
