#include "lib/pn532.h"

#include <Wire.h>

#include "pins.h"

PN532 pn532;

namespace {
constexpr uint8_t ADDR = PN532_I2C_ADDR;
constexpr uint8_t PREAMBLE = 0x00;
constexpr uint8_t STARTCODE1 = 0x00, STARTCODE2 = 0xFF;
constexpr uint8_t POSTAMBLE = 0x00;
constexpr uint8_t HOSTTOPN532 = 0xD4;
constexpr uint8_t PN532TOHOST = 0xD5;

uint8_t s_name[8];
}

bool PN532::waitReady(uint16_t timeoutMs) {
    uint32_t start = millis();
    do {
        if (Wire.requestFrom((int)ADDR, 1) == 1) {
            uint8_t status = Wire.read();
            if (status & 0x01) return true;
        }
        delay(2);
    } while ((millis() - start) < timeoutMs);
    return false;
}

bool PN532::sendCommand(const uint8_t *data, uint8_t len) {

    uint8_t frame[64];
    uint8_t n = 0;
    uint8_t lcs = (uint8_t)(~(len + 1) + 1);
    uint8_t sum = 0;
    frame[n++] = PREAMBLE;
    frame[n++] = STARTCODE1;
    frame[n++] = STARTCODE2;
    frame[n++] = (uint8_t)(len + 1);
    frame[n++] = lcs;
    for (uint8_t i = 0; i < len; i++) {
        frame[n++] = data[i];
        sum += data[i];
    }
    frame[n++] = (uint8_t)(~sum + 1);
    frame[n++] = POSTAMBLE;

    Wire.beginTransmission(ADDR);
    Wire.write(frame, n);
    if (Wire.endTransmission() != 0) return false;
    return readAck(100);
}

bool PN532::readAck(uint16_t timeoutMs) {
    uint32_t start = millis();
    do {
        if (!waitReady(timeoutMs)) return false;
        uint8_t buf[16];
        int got = Wire.requestFrom((int)ADDR, (int)sizeof(buf));
        if (got <= 0) return false;
        for (int i = 0; i < got; i++) buf[i] = Wire.read();

        if (buf[1] == 0x00 && buf[2] == 0x00 && buf[3] == 0xFF &&
            buf[4] == 0x00 && buf[5] == 0xFF && buf[6] == 0x00)
            return true;
        delay(2);
    } while ((millis() - start) < timeoutMs);
    return false;
}

bool PN532::readFrame(uint8_t *out, uint8_t &outLen, uint16_t timeoutMs) {
    outLen = 0;
    if (!waitReady(timeoutMs)) return false;
    uint8_t buf[96];
    int got = Wire.requestFrom((int)ADDR, (int)sizeof(buf));
    if (got < 8) return false;
    for (int i = 0; i < got; i++) buf[i] = Wire.read();
    if (buf[1] != 0x00 || buf[2] != 0x00 || buf[3] != 0xFF) return false;
    uint8_t len = buf[4];
    uint8_t lcs = buf[5];
    if ((uint8_t)(len + lcs) != 0x00) return false;
    if (len < 2 || len > 64) return false;
    if (buf[6] != PN532TOHOST) return false;
    if (buf[6 + len] != POSTAMBLE) return false;

    uint8_t sum = 0;
    for (uint8_t i = 0; i < len; i++) sum += buf[6 + i];
    if ((uint8_t)(sum + buf[6 + len - 1]) != 0x00) {

    }
    outLen = (uint8_t)(len - 2);
    memcpy(out, &buf[7], outLen);
    return true;
}

bool PN532::begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);
    delay(50);

    const uint8_t cmd[] = {HOSTTOPN532, 0x02};
    uint8_t resp[16];
    uint8_t n = 0;
    if (!sendCommand(cmd, sizeof(cmd))) { s_ok = false; return false; }
    if (!readFrame(resp, n, 300) || n < 4) { s_ok = false; return false; }
    if (resp[0] != 0x03) { s_ok = false; return false; }
    s_fw = ((uint32_t)resp[1] << 24) | ((uint32_t)resp[2] << 16) |
           ((uint32_t)resp[3] << 8) | (n > 4 ? resp[4] : 0);
    s_ok = true;

    const uint8_t sam[] = {HOSTTOPN532, 0x14, 0x01};
    sendCommand(sam, sizeof(sam));
    uint8_t tmp[8];
    readFrame(tmp, n, 200);
    return true;
}

bool PN532::present() { return s_ok; }
uint32_t PN532::firmware() { return s_fw; }

bool PN532::readTagA(NfcTag &tag, uint16_t timeoutMs) {
    tag.valid = false;
    if (!s_ok) return false;
    const uint8_t cmd[] = {HOSTTOPN532, 0x4A, 0x01, 0x00};
    if (!sendCommand(cmd, sizeof(cmd))) return false;

    uint8_t resp[48];
    uint8_t n = 0;
    if (!readFrame(resp, n, timeoutMs)) return false;

    if (n < 6 || resp[0] != 0x4B || resp[1] < 1) return false;

    uint8_t i = 2;
    (void)resp[i++];
    tag.sensRes = (uint16_t)((resp[i] << 8) | resp[i + 1]);
    i += 2;
    tag.selRes = resp[i++];
    uint8_t idLen = resp[i++];
    if (idLen > 10 || (uint8_t)(i + idLen) > n) return false;
    tag.uidLen = idLen;
    memcpy(tag.uid, &resp[i], idLen);
    tag.valid = true;
    memcpy(s_name, tag.uid, min((int)idLen, 8));
    return true;
}

const char *PN532::cardName() const { return "ISO14443A"; }
