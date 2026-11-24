#pragma once

#include <Arduino.h>

#include "assets/gfxfont.h"

namespace gfx {

extern const GFXfont *F_SMALL;
extern const GFXfont *F_SMALL_B;
extern const GFXfont *F_BODY;
extern const GFXfont *F_BODY_B;
extern const GFXfont *F_BIG;
extern const GFXfont *F_BIG_B;

namespace col {
constexpr uint16_t BLACK   = 0x0000;
constexpr uint16_t BG      = 0x0000;
constexpr uint16_t BG2     = 0x0861;
constexpr uint16_t PANEL   = 0x18C3;
constexpr uint16_t PANEL2  = 0x2104;
constexpr uint16_t PANEL3  = 0x2945;
constexpr uint16_t LINE    = 0x3186;
constexpr uint16_t TEXT    = 0xFFFF;
constexpr uint16_t TEXT2   = 0xBDF7;
constexpr uint16_t DIM     = 0x7BEF;
constexpr uint16_t DIM2    = 0x4A49;
constexpr uint16_t ORANGE  = 0xFC80;
constexpr uint16_t OK      = 0x2FE5;
constexpr uint16_t WARN    = 0xFD20;
constexpr uint16_t ERR     = 0xF800;
constexpr uint16_t BLUE    = 0x2D7F;
constexpr uint16_t VIOLET  = 0x9A7F;

extern uint16_t accent;
inline uint16_t accentDim() { return (uint16_t)((accent >> 1) & 0x7BEF); }
inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
}

enum class Icon : uint8_t {
    None = 0,
    Wifi, Bluetooth, SubGhz, Nrf, Nfc, Ir, Gps, Game, Folder, Chip,
    Gear, Info, Battery, Sd, Terminal, Radar, Clock, Speaker, Signal,
    Lock, Search, Play, Trash, Map, Usb, Web, Log, Star, Crosshair,
    ArrowUp, ArrowDown, Warning, Check, Key
};

void fillScreen(uint16_t c);
void fillRect(int x, int y, int w, int h, uint16_t c);
void rect(int x, int y, int w, int h, uint16_t c);
void hline(int x, int y, int w, uint16_t c);
void vline(int x, int y, int h, uint16_t c);
void line(int x0, int y0, int x1, int y1, uint16_t c);
void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c);
void roundRect(int x, int y, int w, int h, int r, uint16_t c);
void circle(int cx, int cy, int r, uint16_t c);
void fillCircle(int cx, int cy, int r, uint16_t c);
void arc(int cx, int cy, int r, int a0, int a1, uint16_t c, int thick = 2);
void triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c,
              bool fill_ = true);
void gradientV(int x, int y, int w, int h, uint16_t top, uint16_t bottom);
void progressBar(int x, int y, int w, int h, int value, int vmax,
                 uint16_t fg, uint16_t bg);
void batteryIcon(int x, int y, int w, int h, int percent, uint16_t fg, uint16_t bg);

int textWidth(const char *s, const GFXfont *f);
int textHeight(const GFXfont *f);
int ascent(const GFXfont *f);
int textWidth5(const char *s, int scale = 1);
int textHeight5(int scale = 1);

void text(int x, int baseline, const char *s, const GFXfont *f, uint16_t fg,
          uint16_t bg);

void textTop(int x, int top, const char *s, const GFXfont *f, uint16_t fg,
             uint16_t bg);
void textCenter(int cx, int baseline, const char *s, const GFXfont *f,
                uint16_t fg, uint16_t bg);
void textRight(int rx, int baseline, const char *s, const GFXfont *f,
               uint16_t fg, uint16_t bg);

void text5(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale = 1);

void drawIcon(Icon icon, int x, int y, int size, uint16_t color);

void ellipsize(char *dst, size_t dstSize, const char *src, int maxPx,
               const GFXfont *f);

}
