#pragma once

#include <Arduino.h>

class NRF24 {
public:
    bool    begin();
    bool    present();
    void    setChannel(uint8_t ch);
    uint8_t channel();
    bool    carrierDetected();
    void    rx();
    void    powerDown();

private:
    bool     s_ok = false;
    uint8_t  s_ch = 76;
};

extern NRF24 nrf24;
