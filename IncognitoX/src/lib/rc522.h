#pragma once

#include <Arduino.h>

struct RfidCard {
    uint8_t  uid[10] = {0};
    uint8_t  uidLen = 0;
    uint16_t atqa = 0;
    uint8_t  sak = 0;
    bool     valid = false;
};

class RC522 {
public:
    bool    begin();
    bool    present();
    uint8_t version();

    bool    scan(RfidCard &card, uint16_t timeoutMs = 60);

    bool    readPages(uint8_t block, uint8_t out[16]);
    bool    writePages(uint8_t block, const uint8_t in[16]);

    void    antennaOn(bool on);
    const char *typeName(uint8_t sak);

private:
    bool     s_ok = false;
    RfidCard s_last;

    bool     transceive(uint8_t *send, uint8_t sendLen, uint8_t *recv,
                        uint16_t &recvLen, uint8_t txLastBits = 0,
                        uint16_t timeoutMs = 60);
    bool     selectCard(RfidCard &card);
    uint8_t  crc(uint8_t *data, uint8_t len, uint8_t *out);
};

extern RC522 rc522;
