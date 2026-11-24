#pragma once

#include <Arduino.h>

enum class Key : uint8_t { Up = 0, Down, Left, Right, Ok, None };
enum class KeyEvent : uint8_t { Press, Repeat, Long };

class Input {
public:
    void begin();
    void poll();

    bool pop(Key &k, KeyEvent &e);
    bool isDown(Key k) const;
    bool anyDown() const;
    uint32_t idleMs() const;

private:
    struct Btn {
        uint8_t  pin = 0;
        bool     raw = false;
        bool     stable = false;
        bool     longSent = false;
        uint32_t lastChange = 0;
        uint32_t nextRepeat = 0;
    };
    static constexpr int N = 5;
    Btn b_[N];

    struct Ev { Key key; KeyEvent ev; };
    Ev      q_[16];
    uint8_t head_ = 0, tail_ = 0;
    uint32_t lastActivity_ = 0;

    void emit(Key k, KeyEvent e);
};

extern Input input;
