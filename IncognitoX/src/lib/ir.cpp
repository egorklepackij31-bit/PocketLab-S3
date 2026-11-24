#include "lib/ir.h"

#include "config.h"
#include "pins.h"

IrPort ir;

namespace {

volatile uint32_t s_stamps[IR_MAX_EDGES];
volatile uint16_t s_count = 0;
volatile uint32_t s_lastEdgeMs = 0;

void IRAM_ATTR isrIr() {
    if (s_count < IR_MAX_EDGES) {
        s_stamps[s_count] = micros();
        s_count = (uint16_t)(s_count + 1);
    }
    s_lastEdgeMs = millis();
}

bool near(uint32_t v, uint32_t target, uint32_t tol) {
    return v > (target - tol) && v < (target + tol);
}

bool s_carrier = false;

void carrierOn() {
    if (!s_carrier) {
        s_carrier = ledcAttach(PIN_IR_TX, 38000, 8);
    }
    if (s_carrier) ledcWrite(PIN_IR_TX, 128);
}

void carrierOff() {
    if (s_carrier) ledcWrite(PIN_IR_TX, 0);
}

void mark(uint32_t us) {
    carrierOn();
    delayMicroseconds(us);
    carrierOff();
}

void space(uint32_t us) { delayMicroseconds(us); }

void sendRawNec(uint32_t data, uint8_t bits, bool samsung) {
    mark(samsung ? 4500 : 9000);
    space(samsung ? 4500 : 4500);
    for (uint8_t i = 0; i < bits; i++) {
        mark(560);
        space((data & (1UL << i)) ? 1690 : 560);
    }
    mark(560);
    space(40000);
}

const IrPort::Btn kSamsung[] = {
    {"Power", 0xE0E040BF}, {"Vol +", 0xE0E0E01F}, {"Vol -", 0xE0E0D02F},
    {"Mute", 0xE0E0F00F},  {"CH +", 0xE0E048B7},  {"CH -", 0xE0E008F7},
    {"Source", 0xE0E0807F},
};
const IrPort::Btn kLG[] = {
    {"Power", 0x20DF10EF}, {"Vol +", 0x20DF40BF}, {"Vol -", 0x20DFC03F},
    {"Mute", 0x20DF906F},  {"CH +", 0x20DF00FF},  {"CH -", 0x20DF807F},
    {"Input", 0x20DFD02F},
};

}

void IrPort::begin() {
    pinMode(PIN_IR_RX, INPUT_PULLUP);
    pinMode(PIN_IR_TX, OUTPUT);
    digitalWrite(PIN_IR_TX, LOW);
    attachInterrupt(digitalPinToInterrupt(PIN_IR_RX), isrIr, CHANGE);
    s_count = 0;
}

int IrPort::rawEdges() { return s_count; }
const uint32_t *IrPort::raw() { return (const uint32_t *)s_stamps; }

int IrPort::durations(uint32_t *out, int max) {
    int n = s_count;
    if (n < 2) return 0;
    int k = 0;
    for (int i = 0; i + 1 < n && k < max; i++) out[k++] = s_stamps[i + 1] - s_stamps[i];
    return k;
}

bool IrPort::receiving() { return (millis() - s_lastEdgeMs) < 300 && s_count > 2; }

bool IrPort::poll(IrCode &out) {
    out.valid = false;

    if (s_count < 5) { s_count = 0; return false; }
    if ((millis() - s_lastEdgeMs) < 22) return false;

    uint16_t n = s_count;
    uint32_t d[IR_MAX_EDGES];
    int dn = durations(d, IR_MAX_EDGES);
    s_count = 0;
    if (dn < 4) return false;
    n = (uint16_t)dn;

    bool nec = near(d[0], 9000, 2200);
    bool samsung = near(d[0], 4500, 1200);
    if (!nec && !samsung) { out.proto = IR_UNKNOWN; return false; }

    uint16_t i = 2;
    uint32_t data = 0;
    uint8_t bits = 0;
    while (i + 1 < n && bits < 32) {
        if (!near(d[i], 560, 320)) break;
        uint32_t sp = d[i + 1];
        bool one;
        if (near(sp, 1690, 500)) one = true;
        else if (near(sp, 560, 320)) one = false;
        else break;
        if (one) data |= (1UL << bits);
        bits++;
        i += 2;
    }

    if (bits < 8) {
        out.proto = IR_UNKNOWN;
        out.bits = bits;
        out.data = data;
        return false;
    }

    out.data = data;
    out.bits = bits;
    out.proto = nec ? IR_NEC : IR_SAMSUNG;
    out.valid = true;
    return true;
}

void IrPort::sendNec(uint32_t data, uint8_t bits) {
    sendRawNec(data, bits, false);
}

void IrPort::sendSamsung(uint32_t data, uint8_t bits) {
    sendRawNec(data, bits, true);
}

void IrPort::sendCode(const IrCode &c) {
    if (c.proto == IR_SAMSUNG) sendSamsung(c.data, c.bits ? c.bits : 32);
    else sendNec(c.data, c.bits ? c.bits : 32);
}

void IrPort::stopCarrier() { carrierOff(); }

const IrPort::Btn *IrPort::tvButtons(int device, int &count) {
    switch (device) {
    case 0: count = sizeof(kSamsung) / sizeof(kSamsung[0]); return kSamsung;
    case 1: count = sizeof(kLG) / sizeof(kLG[0]); return kLG;
    default: count = 0; return nullptr;
    }
}

const char *IrPort::tvName(int device) {
    switch (device) {
    case 0: return "Samsung (32-bit)";
    case 1: return "LG (NEC)";
    default: return "?";
    }
}

int IrPort::tvCount() { return 2; }

const char *irProtoName(uint8_t proto) {
    switch (proto) {
    case IR_NEC: return "NEC";
    case IR_SAMSUNG: return "Samsung";
    default: return "unknown";
    }
}
