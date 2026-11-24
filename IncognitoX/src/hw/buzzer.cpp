#include "hw/buzzer.h"

#include "pins.h"

namespace buzzer {
namespace {

struct Note { uint16_t freq; uint16_t ms; };

struct Queue {
    Note     n[24];
    uint8_t  count = 0;
    uint8_t  idx = 0;
    uint32_t until = 0;
    bool     playing = false;
} q;

bool     s_enabled = true;
uint8_t  s_vol = 50;
bool     s_pwm = false;

const Note MEL_START_SEQ[] = {{1320, 70}, {1760, 70}, {2093, 120}};
const Note MEL_ERROR_SEQ[] = {{660, 120}, {330, 220}};
const Note MEL_ALARM_SEQ[] = {{2093, 90}, {0, 60}, {2093, 90}, {0, 60},
                              {2637, 160}};

void writeTone(uint16_t freq) {
    if (!s_pwm) return;
    if (freq == 0) {
        ledcWriteTone(PIN_BUZZER, 0);
        return;
    }
    ledcWriteTone(PIN_BUZZER, freq);

    ledcWrite(PIN_BUZZER, (uint32_t)s_vol * 128 / 100);
}

void push(uint16_t f, uint16_t ms) {
    if (!s_enabled || s_vol == 0) return;
    if (q.count >= 24) return;
    q.n[q.count].freq = f;
    q.n[q.count].ms = ms;
    q.count++;
}

}

void begin() {
    s_pwm = ledcAttach(PIN_BUZZER, 2000, 8);
    ledcWrite(PIN_BUZZER, 0);
}

void enable(bool on) {
    s_enabled = on;
    if (!on) stop();
}

bool enabled() { return s_enabled; }

void setVolume(uint8_t percent) {
    s_vol = percent > 100 ? 100 : percent;
}

void stop() {
    q.count = q.idx = 0;
    q.playing = false;
    writeTone(0);
}

void tick() {
    uint32_t now = millis();
    if (q.playing) {
        if ((int32_t)(now - q.until) < 0) return;
        writeTone(0);
        q.playing = false;
        q.idx++;
    }
    if (q.idx >= q.count) {
        q.count = q.idx = 0;
        return;
    }
    Note &n = q.n[q.idx];
    q.until = now + n.ms;
    q.playing = true;
    writeTone(n.freq);
}

void beep(uint16_t freq, uint16_t ms) { push(freq, ms); }
void click() { push(3000, 8); }
void ok() { push(1800, 40); push(2400, 60); }
void err() { push(500, 90); push(300, 140); }
void start() { play(MEL_START); }

void play(uint8_t melody) {
    stop();
    const Note *seq = nullptr;
    uint8_t len = 0;
    switch (melody) {
    case MEL_START: seq = MEL_START_SEQ; len = 3; break;
    case MEL_ERROR: seq = MEL_ERROR_SEQ; len = 2; break;
    case MEL_ALARM: seq = MEL_ALARM_SEQ; len = 5; break;
    default: return;
    }
    for (uint8_t i = 0; i < len; i++) push(seq[i].freq, seq[i].ms);
}

}
