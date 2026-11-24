#pragma once

#include <Arduino.h>

class CC1101 {
public:
    bool    begin();
    bool    present();
    uint8_t version();
    uint8_t partnum();

    void    setFrequency(float mhz);
    float   frequency();

    void    setRx();
    void    setIdle();
    void    setOOK();
    void    set2FSK();

    float   rssiDbm();
    uint8_t marcState();
    uint8_t pktStatus();

    void     captureStart(uint16_t maxEdges = 0);
    void     captureStop();
    bool     capturing();
    uint16_t capturedEdges();
    const uint32_t *captured();
    uint32_t captureSpanUs();
    bool     captureTimedOut(uint32_t us = 3000000);

    int      readFifo(uint8_t *out, int max);
};

extern CC1101 cc1101;
