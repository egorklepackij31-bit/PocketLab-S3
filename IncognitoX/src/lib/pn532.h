#pragma once

#include <Arduino.h>

struct NfcTag {
    uint8_t  uid[10] = {0};
    uint8_t  uidLen = 0;
    uint16_t sensRes = 0;
    uint8_t  selRes = 0;
    bool     valid = false;
};

class PN532 {
public:
    bool     begin();
    bool     present();
    uint32_t firmware();
    bool     readTagA(NfcTag &tag, uint16_t timeoutMs = 400);
    const char *cardName() const;

private:
    bool     s_ok = false;
    uint32_t s_fw = 0;

    bool     sendCommand(const uint8_t *data, uint8_t len);
    bool     readAck(uint16_t timeoutMs);
    bool     readFrame(uint8_t *out, uint8_t &outLen, uint16_t timeoutMs);
    bool     waitReady(uint16_t timeoutMs);
};

extern PN532 pn532;
