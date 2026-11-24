#include "lib/rc522.h"

#include <SPI.h>

#include "pins.h"

RC522 rc522;

namespace {

constexpr uint8_t R_COMMAND = 0x01, R_COM_IEN = 0x02, R_COM_IRQ = 0x04,
                  R_ERROR = 0x06, R_STATUS2 = 0x08, R_FIFO_DATA = 0x09,
                  R_FIFO_LEVEL = 0x0A, R_CONTROL = 0x0C, R_BIT_FRAMING = 0x0D,
                  R_COLL = 0x0E, R_MODE = 0x11, R_TX_CONTROL = 0x14,
                  R_TX_ASK = 0x15, R_T_MODE = 0x2A, R_T_PRESCALER = 0x2B,
                  R_T_RELOAD_H = 0x2C, R_T_RELOAD_L = 0x2D, R_VERSION = 0x37;

constexpr uint8_t C_IDLE = 0x00, C_TRANSCEIVE = 0x0C, C_SOFT_RESET = 0x0F;

constexpr uint8_t PICC_REQA = 0x26, PICC_SEL_CL1 = 0x93,
                  PICC_SEL_CL2 = 0x95, PICC_SEL_CL3 = 0x97;

SPISettings s_spi(6000000, MSBFIRST, SPI_MODE0);

inline void csLow()  { digitalWrite(PIN_RC522_CS, LOW); }
inline void csHigh() { digitalWrite(PIN_RC522_CS, HIGH); }

void wreg(uint8_t reg, uint8_t val) {
    SPI.beginTransaction(s_spi);
    csLow();
    SPI.transfer((reg << 1) & 0x7E);
    SPI.transfer(val);
    csHigh();
    SPI.endTransaction();
}

uint8_t rreg(uint8_t reg) {
    SPI.beginTransaction(s_spi);
    csLow();
    SPI.transfer(((reg << 1) & 0x7E) | 0x80);
    uint8_t v = SPI.transfer(0);
    csHigh();
    SPI.endTransaction();
    return v;
}

void setMask(uint8_t reg, uint8_t mask) { wreg(reg, rreg(reg) | mask); }
void clrMask(uint8_t reg, uint8_t mask) { wreg(reg, rreg(reg) & (uint8_t)~mask); }

void antenna(bool on) {
    if (on) setMask(R_TX_CONTROL, 0x03);
    else clrMask(R_TX_CONTROL, 0x03);
}

}

bool RC522::begin() {
    pinMode(PIN_RC522_CS, OUTPUT);
    pinMode(PIN_RC522_RST, OUTPUT);
    digitalWrite(PIN_RC522_CS, HIGH);
    digitalWrite(PIN_RC522_RST, HIGH);
    delay(5);

    wreg(R_COMMAND, C_SOFT_RESET);
    delay(60);
    wreg(R_T_MODE, 0x8D);
    wreg(R_T_PRESCALER, 0x3E);
    wreg(R_T_RELOAD_H, 0x00);
    wreg(R_T_RELOAD_L, 30);
    wreg(R_TX_ASK, 0x40);
    wreg(R_MODE, 0x3D);
    antenna(false);

    uint8_t v = rreg(R_VERSION);
    s_ok = (v != 0x00 && v != 0xFF);
    return s_ok;
}

bool RC522::present() { return s_ok; }
uint8_t RC522::version() { return rreg(R_VERSION); }
void RC522::antennaOn(bool on) { antenna(on); }

bool RC522::transceive(uint8_t *send, uint8_t sendLen, uint8_t *recv,
                       uint16_t &recvLen, uint8_t txLastBits, uint16_t timeoutMs) {
    recvLen = 0;
    wreg(R_COMMAND, C_IDLE);
    wreg(R_COM_IRQ, 0x7F);
    wreg(R_FIFO_LEVEL, 0x80);
    for (uint8_t i = 0; i < sendLen; i++) wreg(R_FIFO_DATA, send[i]);
    wreg(R_BIT_FRAMING, (uint8_t)((txLastBits & 0x07) | (sendLen ? 0x00 : 0x80)));
    wreg(R_COMMAND, C_TRANSCEIVE);
    setMask(R_BIT_FRAMING, 0x80);

    uint32_t start = millis();
    uint8_t irq = 0;
    do {
        irq = rreg(R_COM_IRQ);
        if (irq & 0x01) break;
    } while ((millis() - start) < timeoutMs);
    clrMask(R_BIT_FRAMING, 0x80);

    if (!(irq & 0x01) && !(irq & 0x20)) return false;
    if (rreg(R_ERROR) & 0x1B) {
        return false;
    }
    uint8_t n = rreg(R_FIFO_LEVEL);
    if (n > 0) {
        for (uint8_t i = 0; i < n && i < 64; i++) recv[i] = rreg(R_FIFO_DATA);
        recvLen = n;
    }
    return true;
}

uint8_t RC522::crc(uint8_t *data, uint8_t len, uint8_t *out) {
    wreg(R_COMMAND, C_IDLE);
    wreg(R_COM_IRQ, 0x04);
    wreg(R_FIFO_LEVEL, 0x80);
    for (uint8_t i = 0; i < len; i++) wreg(R_FIFO_DATA, data[i]);
    wreg(R_COMMAND, 0x03);
    uint32_t start = millis();
    while (!(rreg(R_COM_IRQ) & 0x04) && (millis() - start) < 50) {}
    wreg(R_COMMAND, C_IDLE);
    if (!(rreg(R_COM_IRQ) & 0x04)) return 0;
    out[0] = rreg(R_FIFO_DATA);
    out[1] = rreg(R_FIFO_DATA);
    return 2;
}

bool RC522::selectCard(RfidCard &card) {
    uint8_t buf[9];
    uint8_t crcOut[2];
    uint16_t recvLen = 0;

    antenna(true);
    uint8_t reqa = PICC_REQA;
    uint8_t atqa[8];
    if (!transceive(&reqa, 1, atqa, recvLen, 0x07, 40) || recvLen < 2) {
        antenna(false);
        return false;
    }
    card.atqa = (uint16_t)((atqa[0] << 8) | atqa[1]);

    buf[0] = PICC_SEL_CL1;
    buf[1] = 0x20;
    if (!transceive(buf, 2, card.uid, recvLen, 0, 40) || recvLen < 5) {
        antenna(false);
        return false;
    }
    card.uidLen = 4;
    card.uid[4] = 0;

    uint8_t sel[9];
    sel[0] = PICC_SEL_CL1;
    sel[1] = 0x70;
    for (int i = 0; i < 5; i++) sel[2 + i] = card.uid[i];
    crc(sel, 7, crcOut);
    sel[7] = crcOut[0];
    sel[8] = crcOut[1];

    uint8_t sakBuf[8];
    if (!transceive(sel, 9, sakBuf, recvLen, 0, 40) || recvLen < 1) {
        antenna(false);
        return false;
    }
    card.sak = sakBuf[0];
    card.valid = true;

    if (!(card.sak & 0x04)) {

    }
    return true;
}

bool RC522::scan(RfidCard &card, uint16_t timeoutMs) {
    card.valid = false;
    if (!s_ok) return false;
    bool ok = selectCard(card);
    if (ok) s_last = card;
    (void)timeoutMs;
    return ok;
}

bool RC522::readPages(uint8_t block, uint8_t out[16]) {
    if (!s_ok || !s_last.valid) return false;
    antenna(true);
    selectCard(s_last);

    uint8_t cmd[4] = {0x30, (uint8_t)block, 0, 0};
    uint8_t crcOut[2];
    crc(cmd, 2, crcOut);
    cmd[2] = crcOut[0];
    cmd[3] = crcOut[1];

    uint8_t recv[24];
    uint16_t recvLen = 0;
    bool ok = transceive(cmd, 4, recv, recvLen, 0, 60);
    if (ok && recvLen >= 18) {
        memcpy(out, recv, 16);
    } else {
        ok = false;
    }
    antenna(false);
    return ok;
}

bool RC522::writePages(uint8_t block, const uint8_t in[16]) {
    if (!s_ok || !s_last.valid) return false;
    antenna(true);
    selectCard(s_last);

    uint8_t cmd[20];
    cmd[0] = 0xA2;
    cmd[1] = block;
    uint8_t crcOut[2];
    crc(cmd, 2, crcOut);
    cmd[2] = crcOut[0];
    cmd[3] = crcOut[1];

    uint16_t recvLen = 0;
    uint8_t ack[8];
    bool ok = transceive(cmd, 4, ack, recvLen, 0, 60);
    if (!ok || recvLen < 1 || ((ack[0] & 0x0F) != 0x0A)) { antenna(false); return false; }

    uint8_t data[18];
    memcpy(data, in, 16);
    crc(data, 16, crcOut);
    data[16] = crcOut[0];
    data[17] = crcOut[1];
    ok = transceive(data, 18, ack, recvLen, 0, 60);
    antenna(false);
    return ok && recvLen >= 1 && ((ack[0] & 0x0F) == 0x0A);
}

const char *RC522::typeName(uint8_t sak) {
    switch (sak) {
    case 0x00: return "Ultralight / NTAG";
    case 0x08: return "MIFARE Classic 1K";
    case 0x09: return "MIFARE Mini";
    case 0x18: return "MIFARE Classic 4K";
    case 0x10: return "MIFARE Plus 2K";
    case 0x11: return "MIFARE Plus 4K";
    case 0x20: return "MIFARE DESFire";
    case 0x28: return "JCOP31";
    case 0x53: return "JCOP31/SL2";
    default:   return "ISO14443A tag";
    }
}
