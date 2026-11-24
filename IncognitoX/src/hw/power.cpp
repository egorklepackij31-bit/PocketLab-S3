#include "hw/power.h"

#include "config.h"
#include "driver/temperature_sensor.h"
#include "pins.h"

namespace power {
namespace {

float    s_volt = 0.0f;
int      s_pct = -1;
uint32_t s_last = 0;
float    s_prev = 0.0f;
bool     s_rise = false;
temperature_sensor_handle_t s_temp = nullptr;

int voltToPercent(float v) {
    static const float table[][2] = {
        {4.20f, 100}, {4.10f, 90}, {4.00f, 80}, {3.92f, 70}, {3.85f, 60},
        {3.80f, 50},  {3.75f, 40}, {3.70f, 30}, {3.65f, 20}, {3.55f, 10},
        {3.40f, 5},   {3.20f, 0},
    };
    if (v >= table[0][0]) return 100;
    for (size_t i = 1; i < sizeof(table) / sizeof(table[0]); i++) {
        if (v >= table[i][0]) {
            float v1 = table[i - 1][0], v2 = table[i][0];
            float p1 = table[i - 1][1], p2 = table[i][1];
            return (int)(p2 + (v - v2) * (p1 - p2) / (v1 - v2));
        }
    }
    return 0;
}

}

void begin() {
#if PIN_BATTERY_ADC >= 0
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_BATTERY_ADC, ADC_11db);
#endif
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&cfg, &s_temp) == ESP_OK) {
        temperature_sensor_enable(s_temp);
    }
}

bool available() {
#if PIN_BATTERY_ADC >= 0
    return s_pct >= 0;
#else
    return false;
#endif
}

void tick() {
    uint32_t now = millis();
    if (now - s_last < 500) return;
    s_last = now;

#if PIN_BATTERY_ADC >= 0
    uint32_t mv = 0;
    for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_BATTERY_ADC);
    mv /= 8;
    float v = (mv / 1000.0f) * BATTERY_DIVIDER;

    if (v > 2.0f && v < 4.65f) {
        s_volt = s_volt == 0.0f ? v : (s_volt * 0.8f + v * 0.2f);
        s_pct = voltToPercent(s_volt);
        if (s_prev > 0.0f) s_rise = (s_volt - s_prev) > 0.004f;
        s_prev = s_volt;
    } else {
        s_pct = -1;
    }
#endif
}

float voltage() { return s_volt; }
int   percent() { return s_pct; }
bool  chargingGuess() { return s_rise; }

float chipTempC() {
    float t = 0;
    if (s_temp && temperature_sensor_get_celsius(s_temp, &t) == ESP_OK) return t;
    return 0;
}

}
