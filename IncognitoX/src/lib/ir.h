#pragma once

#include <Arduino.h>

enum IrProto : uint8_t { IR_NEC = 0, IR_SAMSUNG = 1, IR_UNKNOWN = 255 };

struct IrCode {
    uint32_t data = 0;
    uint8_t  bits = 0;
    uint8_t  proto = IR_UNKNOWN;
    bool     valid = false;
};

const char *irProtoName(uint8_t proto);

class IrPort {
public:
    void begin();
    bool poll(IrCode &out);
    bool receiving();
    int  rawEdges();
    const uint32_t *raw();
    int  durations(uint32_t *out, int max);

    void sendNec(uint32_t data, uint8_t bits = 32);
    void sendSamsung(uint32_t data, uint8_t bits = 32);
    void sendCode(const IrCode &c);
    void stopCarrier();

    struct Btn { const char *name; uint32_t data; };
    static const Btn *tvButtons(int device, int &count);
    static const char *tvName(int device);
    static int tvCount();
};

extern IrPort ir;
