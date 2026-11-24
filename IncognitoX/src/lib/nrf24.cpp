#include "lib/nrf24.h"

#include <SPI.h>

#include "pins.h"

NRF24 nrf24;

namespace {

constexpr uint8_t R_CONFIG = 0x00, R_EN_AA = 0x01, R_EN_RXADDR = 0x02,
                  R_SETUP_AW = 0x03, R_SETUP_RETR = 0x04, R_RF_CH = 0x05,
                  R_RF_SETUP = 0x06, R_RPD = 0x09, R_RX_ADDR_P0 = 0x0A,
                  R_RX_PW_P0 = 0x11, R_DYNPD = 0x1C, R_FEATURE = 0x1D;
constexpr uint8_t C_R_REG = 0x00, C_W_REG = 0x20, C_FLUSH_TX = 0xE1,
                  C_FLUSH_RX = 0xE2, C_NOP = 0xFF;

SPISettings s_spi(8000000, MSBFIRST, SPI_MODE0);

inline void csLow()  { digitalWrite(PIN_NRF_CS, LOW); }
inline void csHigh() { digitalWrite(PIN_NRF_CS, HIGH); }

void wreg(uint8_t reg, uint8_t val) {
    SPI.beginTransaction(s_spi);
    csLow();
    SPI.transfer(C_W_REG | reg);
    SPI.transfer(val);
    csHigh();
    SPI.endTransaction();
}

uint8_t rreg(uint8_t reg) {
    SPI.beginTransaction(s_spi);
    csLow();
    SPI.transfer(C_R_REG | reg);
    uint8_t v = SPI.transfer(0);
    csHigh();
    SPI.endTransaction();
    return v;
}

void wregBuf(uint8_t reg, const uint8_t *buf, uint8_t len) {
    SPI.beginTransaction(s_spi);
    csLow();
    SPI.transfer(C_W_REG | reg);
    for (uint8_t i = 0; i < len; i++) SPI.transfer(buf[i]);
    csHigh();
    SPI.endTransaction();
}

void command(uint8_t cmd) {
    SPI.beginTransaction(s_spi);
    csLow();
    SPI.transfer(cmd);
    csHigh();
    SPI.endTransaction();
}

}

bool NRF24::begin() {
    pinMode(PIN_NRF_CS, OUTPUT);
    pinMode(PIN_NRF_CE, OUTPUT);
    digitalWrite(PIN_NRF_CS, HIGH);
    digitalWrite(PIN_NRF_CE, LOW);
    delay(5);

    wreg(R_RF_CH, 0x4C);
    uint8_t back = rreg(R_RF_CH);
    s_ok = (back == 0x4C);

    if (!s_ok) return false;

    wreg(R_CONFIG, 0x0B);
    wreg(R_EN_AA, 0x00);
    wreg(R_EN_RXADDR, 0x01);
    wreg(R_SETUP_AW, 0x03);
    wreg(R_SETUP_RETR, 0x00);
    wreg(R_RF_SETUP, 0x06);
    { const uint8_t addr[5] = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7};
      wregBuf(R_RX_ADDR_P0, addr, 5); }
    wreg(R_RX_PW_P0, 32);
    wreg(R_DYNPD, 0x00);
    wreg(R_FEATURE, 0x00);
    command(C_FLUSH_RX);
    command(C_FLUSH_TX);

    setChannel(76);
    rx();
    delay(5);
    return true;
}

bool NRF24::present() { return s_ok; }

void NRF24::setChannel(uint8_t ch) {
    if (ch > 125) ch = 125;
    s_ch = ch;
    wreg(R_RF_CH, ch);
}

uint8_t NRF24::channel() { return s_ch; }

void NRF24::rx() {
    wreg(R_CONFIG, 0x0F);
    digitalWrite(PIN_NRF_CE, HIGH);
    delayMicroseconds(200);
}

void NRF24::powerDown() {
    digitalWrite(PIN_NRF_CE, LOW);
    wreg(R_CONFIG, 0x08);
}

bool NRF24::carrierDetected() {

    uint8_t cfg = rreg(R_CONFIG);
    if (!(cfg & 0x01)) rx();
    delayMicroseconds(180);
    return (rreg(R_RPD) & 0x01) != 0;
}
