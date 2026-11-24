#include "hw/input.h"

#include "config.h"
#include "pins.h"

Input input;

namespace {
const uint8_t kPins[5] = {PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_LEFT, PIN_BTN_RIGHT,
                          PIN_BTN_OK};
}

void Input::begin() {
    for (int i = 0; i < N; i++) {
        b_[i].pin = kPins[i];
        pinMode(b_[i].pin, INPUT_PULLUP);
        b_[i].raw = b_[i].stable = (digitalRead(b_[i].pin) == LOW);
        b_[i].lastChange = millis();
    }
    lastActivity_ = millis();
}

void Input::emit(Key k, KeyEvent e) {
    uint8_t next = (uint8_t)((head_ + 1) % 16);
    if (next == tail_) return;
    q_[head_].key = k;
    q_[head_].ev = e;
    head_ = next;
    lastActivity_ = millis();
}

void Input::poll() {
    const uint32_t now = millis();
    for (int i = 0; i < N; i++) {
        Btn &b = b_[i];
        bool pressed = (digitalRead(b.pin) == LOW);

        if (pressed != b.raw) {
            b.raw = pressed;
            b.lastChange = now;
            continue;
        }
        if (now - b.lastChange < KEY_DEBOUNCE_MS) continue;

        Key key = (Key)i;
        if (pressed && !b.stable) {
            b.stable = true;
            b.longSent = false;
            b.nextRepeat = now + KEY_REPEAT_DELAY;
            emit(key, KeyEvent::Press);
        } else if (pressed && b.stable) {
            if (!b.longSent && now - b.lastChange >= KEY_LONG_MS) {
                b.longSent = true;
                emit(key, KeyEvent::Long);
            } else if ((int32_t)(now - b.nextRepeat) >= 0) {
                b.nextRepeat = now + KEY_REPEAT_MS;
                emit(key, KeyEvent::Repeat);
            }
        } else if (!pressed && b.stable) {
            b.stable = false;
        }
    }
}

bool Input::pop(Key &k, KeyEvent &e) {
    if (head_ == tail_) return false;
    k = q_[tail_].key;
    e = q_[tail_].ev;
    tail_ = (uint8_t)((tail_ + 1) % 16);
    return true;
}

bool Input::isDown(Key k) const {
    int i = (int)k;
    if (i < 0 || i >= N) return false;
    return b_[i].stable;
}

bool Input::anyDown() const {
    for (int i = 0; i < N; i++)
        if (b_[i].stable) return true;
    return false;
}

uint32_t Input::idleMs() const { return millis() - lastActivity_; }
