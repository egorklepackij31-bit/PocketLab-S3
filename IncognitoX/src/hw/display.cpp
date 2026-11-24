#include "hw/display.h"

#include <SPI.h>

#include "config.h"
#include "pins.h"

namespace display {
namespace {

constexpr int16_t CHUNK = 320;
uint16_t s_line[CHUNK];
uint8_t  s_bytes[CHUNK * 2];

SPISettings s_spi(TFT_SPI_HZ, MSBFIRST, SPI_MODE0);

int16_t s_w = 320, s_h = 240;
uint8_t s_rot = 1;
uint8_t s_bl = 80;
bool    s_blReady = false;
bool    s_inPixels = false;

inline void csLow()  { digitalWrite(PIN_TFT_CS, LOW); }
inline void csHigh() { digitalWrite(PIN_TFT_CS, HIGH); }

void cmd(uint8_t c) {
    SPI.beginTransaction(s_spi);
    digitalWrite(PIN_TFT_DC, LOW);
    csLow();
    SPI.transfer(c);
    csHigh();
    SPI.endTransaction();
}

void data(const uint8_t *d, size_t n) {
    SPI.beginTransaction(s_spi);
    digitalWrite(PIN_TFT_DC, HIGH);
    csLow();
    SPI.writeBytes(d, n);
    csHigh();
    SPI.endTransaction();
}

void data8(uint8_t v) { data(&v, 1); }

void cmdData(uint8_t c, const uint8_t *d, size_t n) {
    cmd(c);
    data(d, n);
}

inline void pack(const uint16_t *src, size_t n) {
    for (size_t i = 0; i < n; i++) {
        s_bytes[i * 2]     = (uint8_t)(src[i] >> 8);
        s_bytes[i * 2 + 1] = (uint8_t)(src[i] & 0xFF);
    }
}

void applyRotation(uint8_t r) {
    static const uint8_t madctl[4] = {0x00, 0x60, 0xC0, 0xA0};
    s_rot = r & 3;
    cmd(0x36);
    data8(madctl[s_rot]);
    if (s_rot & 1) { s_w = 320; s_h = 240; }
    else           { s_w = 240; s_h = 320; }
}

}

void init() {
    pinMode(PIN_TFT_CS, OUTPUT);
    pinMode(PIN_TFT_DC, OUTPUT);
    pinMode(PIN_TFT_RST, OUTPUT);
    digitalWrite(PIN_TFT_CS, HIGH);
    digitalWrite(PIN_TFT_DC, HIGH);

    if (!s_blReady) {
        ledcAttach(PIN_TFT_BL, 5000, 8);
        s_blReady = true;
    }
    ledcWrite(PIN_TFT_BL, 0);

    SPI.begin(PIN_TFT_SCLK, PIN_TFT_MISO, PIN_TFT_MOSI, -1);

    digitalWrite(PIN_TFT_RST, LOW);
    delay(20);
    digitalWrite(PIN_TFT_RST, HIGH);
    delay(120);

    cmd(0x01);
    delay(150);

    cmd(0x11);
    delay(120);

    cmd(0x3A); data8(0x55);
    cmd(0x36); data8(0x00);

    { const uint8_t p[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
      cmdData(0xB2, p, sizeof(p)); }
    { const uint8_t p[] = {0x35}; cmdData(0xB7, p, sizeof(p)); }
    { const uint8_t p[] = {0x19}; cmdData(0xBB, p, sizeof(p)); }
    { const uint8_t p[] = {0x2C}; cmdData(0xC0, p, sizeof(p)); }
    { const uint8_t p[] = {0x01}; cmdData(0xC2, p, sizeof(p)); }
    { const uint8_t p[] = {0x12}; cmdData(0xC3, p, sizeof(p)); }
    { const uint8_t p[] = {0x20}; cmdData(0xC4, p, sizeof(p)); }
    { const uint8_t p[] = {0x0F}; cmdData(0xC6, p, sizeof(p)); }
    { const uint8_t p[] = {0xA4, 0xA1}; cmdData(0xD0, p, sizeof(p)); }
    { const uint8_t p[] = {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54,
                           0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23};
      cmdData(0xE0, p, sizeof(p)); }
    { const uint8_t p[] = {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44,
                           0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23};
      cmdData(0xE1, p, sizeof(p)); }

    if (TFT_INVERT) { cmd(0x21); } else { cmd(0x20); }
    cmd(0x13);
    cmd(0x29);
    delay(20);

    applyRotation(TFT_ROTATION);
    fillScreen(0x0000);
    backlight(80);
}

int width()  { return s_w; }
int height() { return s_h; }

void setRotation(uint8_t r) {
    applyRotation(r);
    fillScreen(0x0000);
}

void setAddrWindow(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s_w) w = s_w - x;
    if (y + h > s_h) h = s_h - y;
    if (w <= 0 || h <= 0) return;

    const uint16_t x0 = x, x1 = x + w - 1;
    const uint16_t y0 = y, y1 = y + h - 1;

    cmd(0x2A);
    { uint8_t b[4] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
      data(b, 4); }
    cmd(0x2B);
    { uint8_t b[4] = {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1};
      data(b, 4); }
    cmd(0x2C);
}

void beginPixels() {
    if (s_inPixels) return;
    SPI.beginTransaction(s_spi);
    digitalWrite(PIN_TFT_DC, HIGH);
    csLow();
    s_inPixels = true;
}

void pushPixels(const uint16_t *pixels, size_t count) {
    bool own = !s_inPixels;
    if (own) beginPixels();
    size_t done = 0;
    while (done < count) {
        size_t n = count - done;
        if (n > CHUNK) n = CHUNK;
        pack(pixels + done, n);
        SPI.writeBytes(s_bytes, n * 2);
        done += n;
    }
    if (own) endPixels();
}

void endPixels() {
    if (!s_inPixels) return;
    csHigh();
    SPI.endTransaction();
    s_inPixels = false;
}

void fillRect(int x, int y, int w, int h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s_w) w = s_w - x;
    if (y + h > s_h) h = s_h - y;
    if (w <= 0 || h <= 0) return;

    int n = w;
    while (n > 0) {
        int c = n > CHUNK ? CHUNK : n;
        for (int i = 0; i < c; i++) s_line[i] = color;
        pack(s_line, c);

        setAddrWindow(x, y, w, h);
        beginPixels();
        for (int row = 0; row < h; row++) SPI.writeBytes(s_bytes, c * 2);
        endPixels();
        x += c;
        n -= c;
    }
}

void fillScreen(uint16_t color) {
    fillRect(0, 0, s_w, s_h, color);
}

void pushImage(int x, int y, int w, int h, const uint16_t *pixels) {
    if (w <= 0 || h <= 0) return;
    setAddrWindow(x, y, w, h);
    beginPixels();
    pushPixels(pixels, (size_t)w * h);
    endPixels();
}

void pushImageMono(int x, int y, int w, int h, const uint8_t *bits,
                   uint16_t fg, uint16_t bg) {
    if (w <= 0 || h <= 0) return;
    setAddrWindow(x, y, w, h);
    beginPixels();
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int idx = row * w + col;
            bool on = bits[idx >> 3] & (0x80 >> (idx & 7));
            s_line[col] = on ? fg : bg;
        }
        const uint16_t *src = s_line;
        int n = w;
        while (n > 0) {
            int c = n > CHUNK ? CHUNK : n;
            pack(src, c);
            SPI.writeBytes(s_bytes, c * 2);
            src += c;
            n -= c;
        }
    }
    endPixels();
}

void backlight(uint8_t percent) {
    if (percent > 100) percent = 100;
    s_bl = percent;
    if (s_blReady) ledcWrite(PIN_TFT_BL, (uint32_t)percent * 255 / 100);
}

uint8_t backlight() { return s_bl; }

void sleep(bool on) {
    if (on) {
        backlight(0);
        cmd(0x10);
        delay(20);
    } else {
        cmd(0x11);
        delay(120);
        backlight(s_bl ? s_bl : 80);
    }
}

void invert(bool on) { cmd(on ? 0x21 : 0x20); }

}
