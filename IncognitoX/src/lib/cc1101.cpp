#include "lib/cc1101.h"

#include <SPI.h>

#include "config.h"
#include "pins.h"

CC1101 cc1101;

namespace {

constexpr uint8_t R_IOCFG2   = 0x00, R_IOCFG0 = 0x02, R_FIFOTHR = 0x03;
constexpr uint8_t R_PKTCTRL1 = 0x07, R_PKTCTRL0 = 0x08, R_CHANNR = 0x0A;
constexpr uint8_t R_FSCTRL1  = 0x0B, R_FSCTRL0 = 0x0C;
constexpr uint8_t R_FREQ2    = 0x0D, R_FREQ1 = 0x0E, R_FREQ0 = 0x0F;
constexpr uint8_t R_MDMCFG4  = 0x10, R_MDMCFG3 = 0x11, R_MDMCFG2 = 0x12;
constexpr uint8_t R_MDMCFG1  = 0x13, R_MDMCFG0 = 0x14, R_DEVIATN = 0x15;
constexpr uint8_t R_MCSM2    = 0x16, R_MCSM1 = 0x17, R_MCSM0 = 0x18;
constexpr uint8_t R_FOCCFG   = 0x19, R_BSCFG = 0x1A, R_AGCCTRL2 = 0x1B;
constexpr uint8_t R_AGCCTRL1 = 0x1C, R_AGCCTRL0 = 0x1D;
constexpr uint8_t R_FREND1   = 0x21, R_FSCAL3 = 0x23, R_FSCAL2 = 0x24;
constexpr uint8_t R_FSCAL1   = 0x25, R_FSCAL0 = 0x26;
constexpr uint8_t R_TEST2    = 0x2C, R_TEST1 = 0x2D, R_TEST0 = 0x2E;
constexpr uint8_t R_PATABLE  = 0x3E;

constexpr uint8_t S_SRES = 0x30, S_SRX = 0x34, S_SIDLE = 0x36, S_SFTX = 0x3B,
                  S_SFRX = 0x3A;

SPISettings s_spi(8000000, MSBFIRST, SPI_MODE0);

bool     s_present = false;
float    s_freq = 433.92f;

volatile uint32_t s_edges[PULSE_MAX_EDGES];
volatile uint16_t s_edgeCount = 0;
volatile uint16_t s_edgeMax = PULSE_MAX_EDGES;
volatile uint32_t s_lastEdgeUs = 0;
volatile uint32_t s_firstEdgeUs = 0;
volatile bool     s_capturing = false;

void IRAM_ATTR isrEdge() {
    if (!s_capturing) return;
    uint32_t now = micros();
    uint16_t idx = s_edgeCount;
    if (idx >= s_edgeMax) {
        s_capturing = false;
        return;
    }
    if (idx == 0) s_firstEdgeUs = now;
    else s_edges[idx - 1] = now - s_lastEdgeUs;
    s_lastEdgeUs = now;
    s_edgeCount = idx + 1;
}

inline void csLow()  { digitalWrite(PIN_CC_CS, LOW); }
inline void csHigh() { digitalWrite(PIN_CC_CS, HIGH); }

void writeReg(uint8_t addr, uint8_t val) {
    SPI.beginTransaction(s_spi);
    csLow();
    SPI.transfer(addr & 0x3F);
    SPI.transfer(val);
    csHigh();
    SPI.endTransaction();
}

uint8_t readReg(uint8_t addr) {
    SPI.beginTransaction(s_spi);
    csLow();
    SPI.transfer(addr | 0xC0);
    uint8_t v = SPI.transfer(0);
    csHigh();
    SPI.endTransaction();
    return v;
}

void writeBurst(uint8_t addr, const uint8_t *data, uint8_t len) {
    SPI.beginTransaction(s_spi);
    csLow();
    SPI.transfer(addr | 0x40);
    for (uint8_t i = 0; i < len; i++) SPI.transfer(data[i]);
    csHigh();
    SPI.endTransaction();
}

uint8_t strobe(uint8_t cmd) {
    SPI.beginTransaction(s_spi);
    csLow();
    uint8_t v = SPI.transfer(cmd);
    csHigh();
    SPI.endTransaction();
    return v;
}

}

bool CC1101::begin() {
    pinMode(PIN_CC_CS, OUTPUT);
    digitalWrite(PIN_CC_CS, HIGH);
    pinMode(PIN_CC_GDO0, INPUT);
    pinMode(PIN_CC_GDO2, INPUT);

    strobe(S_SRES);
    delay(3);

    uint8_t ver = readReg(0x31);
    uint8_t part = readReg(0x30);
    s_present = (ver == 0x14 || ver == 0x04 || ver == 0x07);

    if (s_present) {
        writeReg(R_IOCFG2, 0x0B);
        writeReg(R_IOCFG0, 0x0D);
        writeReg(R_FIFOTHR, 0x47);
        writeReg(R_PKTCTRL1, 0x04);
        writeReg(R_PKTCTRL0, 0x32);
        writeReg(R_FSCTRL1, 0x06);
        writeReg(R_FSCTRL0, 0x00);
        writeReg(R_MDMCFG4, 0xC7);
        writeReg(R_MDMCFG3, 0x83);
        writeReg(R_MDMCFG2, 0x30);
        writeReg(R_MDMCFG1, 0x02);
        writeReg(R_MDMCFG0, 0xF8);
        writeReg(R_DEVIATN, 0x15);
        writeReg(R_MCSM2, 0x07);
        writeReg(R_MCSM1, 0x30);
        writeReg(R_MCSM0, 0x18);
        writeReg(R_FOCCFG, 0x16);
        writeReg(R_BSCFG, 0x6C);
        writeReg(R_AGCCTRL2, 0x03);
        writeReg(R_AGCCTRL1, 0x40);
        writeReg(R_AGCCTRL0, 0x91);
        writeReg(R_FREND1, 0xB6);
        writeReg(R_FSCAL3, 0xEA);
        writeReg(R_FSCAL2, 0x2A);
        writeReg(R_FSCAL1, 0x00);
        writeReg(R_FSCAL0, 0x1F);
        writeReg(R_TEST2, 0x81);
        writeReg(R_TEST1, 0x35);
        writeReg(R_TEST0, 0x09);
        { const uint8_t pa[8] = {0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
          writeBurst(R_PATABLE, pa, 8); }

        setFrequency(s_freq);
        strobe(S_SIDLE);
        strobe(S_SRX);
        delay(10);
        if (readReg(0x35) == 0) {
            strobe(S_SRX);
        }
    }

    attachInterrupt(digitalPinToInterrupt(PIN_CC_GDO0), isrEdge, CHANGE);
    return s_present;
}

bool    CC1101::present() { return s_present; }
uint8_t CC1101::version() { return readReg(0x31); }
uint8_t CC1101::partnum() { return readReg(0x30); }

void CC1101::setFrequency(float mhz) {
    s_freq = mhz;
    uint32_t f = (uint32_t)((mhz * 65536.0f) / 26.0f + 0.5f);
    uint8_t base[3] = {(uint8_t)((f >> 16) & 0xFF), (uint8_t)((f >> 8) & 0xFF),
                       (uint8_t)(f & 0xFF)};
    writeReg(R_FREQ2, base[0]);
    writeReg(R_FREQ1, base[1]);
    writeReg(R_FREQ0, base[2]);
}

float CC1101::frequency() { return s_freq; }

void CC1101::setRx() {
    uint8_t m = readReg(0x35);
    if (m != 0x0D) {
        strobe(S_SIDLE);
        strobe(S_SFRX);
        strobe(S_SRX);
    }
}

void CC1101::setIdle() { strobe(S_SIDLE); }

void CC1101::setOOK() { writeReg(R_MDMCFG2, 0x30); setRx(); }
void CC1101::set2FSK() { writeReg(R_MDMCFG2, 0x04); setRx(); }

float CC1101::rssiDbm() {
    int8_t raw = (int8_t)readReg(0x34);
    int v = raw;
    if (v >= 128) v -= 256;
    return (v / 2.0f) - 74.0f;
}

uint8_t CC1101::marcState() { return readReg(0x35) & 0x1F; }
uint8_t CC1101::pktStatus() { return readReg(0x38); }

int CC1101::readFifo(uint8_t *out, int max) {
    int n = (int)(readReg(0x3B) & 0x7F);
    if (n > max) n = max;
    if (n <= 0) return 0;
    SPI.beginTransaction(s_spi);
    csLow();
    SPI.transfer(0x3F | 0xC0);
    for (int i = 0; i < n; i++) out[i] = SPI.transfer(0);
    csHigh();
    SPI.endTransaction();
    return n;
}

void CC1101::captureStart(uint16_t maxEdges) {
    s_edgeCount = 0;
    s_edgeMax = maxEdges ? min(maxEdges, (uint16_t)PULSE_MAX_EDGES)
                         : (uint16_t)PULSE_MAX_EDGES;
    s_lastEdgeUs = micros();
    s_firstEdgeUs = s_lastEdgeUs;
    s_capturing = true;
}

void CC1101::captureStop() { s_capturing = false; }

bool CC1101::capturing() { return s_capturing && s_edgeCount < s_edgeMax; }

uint16_t CC1101::capturedEdges() { return s_edgeCount; }
const uint32_t *CC1101::captured() { return (const uint32_t *)s_edges; }

uint32_t CC1101::captureSpanUs() {
    if (s_edgeCount == 0) return 0;
    return s_lastEdgeUs - s_firstEdgeUs;
}

bool CC1101::captureTimedOut(uint32_t us) {
    return (micros() - s_lastEdgeUs) > us;
}
