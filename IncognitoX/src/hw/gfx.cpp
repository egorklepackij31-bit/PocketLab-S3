#include "hw/gfx.h"

#include <Arduino.h>
#include <math.h>

#include "assets/Roboto9pt7b.h"
#include "assets/Roboto12pt7b.h"
#include "assets/Roboto18pt7b.h"
#include "assets/RobotoBold9pt7b.h"
#include "assets/RobotoBold12pt7b.h"
#include "assets/RobotoBold18pt7b.h"
#include "assets/font5x7.h"
#include "hw/display.h"

namespace gfx {

const GFXfont *F_SMALL   = &Roboto9pt7b;
const GFXfont *F_SMALL_B = &RobotoBold9pt7b;
const GFXfont *F_BODY    = &Roboto12pt7b;
const GFXfont *F_BODY_B  = &RobotoBold12pt7b;
const GFXfont *F_BIG     = &Roboto18pt7b;
const GFXfont *F_BIG_B   = &RobotoBold18pt7b;

uint16_t col::accent = col::ORANGE;

namespace {

uint16_t gbuf[72 * 48];

inline uint32_t utf8Next(const char **sp) {
    const uint8_t *s = (const uint8_t *)*sp;
    uint32_t cp = 0;
    int len = 0;
    if (s[0] < 0x80) { *sp += 1; return s[0]; }
    if ((s[0] & 0xE0) == 0xC0) { len = 2; cp = s[0] & 0x1Fu; }
    else if ((s[0] & 0xF0) == 0xE0) { len = 3; cp = s[0] & 0x0Fu; }
    else if ((s[0] & 0xF8) == 0xF0) { len = 4; cp = s[0] & 0x07u; }
    for (int i = 1; i < len; i++) { cp = (cp << 6) | (s[i] & 0x3Fu); }
    *sp += len;
    return cp;
}

inline void px(int x, int y, uint16_t c) { display::fillRect(x, y, 1, 1, c); }

void thickLine(int x0, int y0, int x1, int y1, int th, uint16_t c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (th <= 1) px(x0, y0, c);
        else display::fillRect(x0 - th / 2, y0 - th / 2, th, th, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void glyph(int x, int baseline, uint32_t ch, const GFXfont *f, uint16_t fg,
           uint16_t bg) {
    if (!f) return;
    if (ch < f->first || ch > f->last) return;
    const GFXglyph *g = &f->glyph[ch - f->first];
    const int w = g->width, h = g->height;
    if (w == 0 || h == 0) return;
    if (w > 72 || h > 48) return;
    const uint8_t *bm = f->bitmap + g->bitmapOffset;

    const int nbits = w * h;
    for (int i = 0; i < nbits; i++) {
        bool on = bm[i >> 3] & (0x80 >> (i & 7));
        gbuf[i] = on ? fg : bg;
    }
    display::pushImage(x + g->xOffset, baseline + g->yOffset, w, h, gbuf);
}

}

void fillScreen(uint16_t c) { display::fillScreen(c); }

void fillRect(int x, int y, int w, int h, uint16_t c) {
    display::fillRect(x, y, w, h, c);
}

void rect(int x, int y, int w, int h, uint16_t c) {
    if (w <= 0 || h <= 0) return;
    hline(x, y, w, c);
    hline(x, y + h - 1, w, c);
    vline(x, y, h, c);
    vline(x + w - 1, y, h, c);
}

void hline(int x, int y, int w, uint16_t c) { display::fillRect(x, y, w, 1, c); }
void vline(int x, int y, int h, uint16_t c) { display::fillRect(x, y, 1, h, c); }

void line(int x0, int y0, int x1, int y1, uint16_t c) {
    thickLine(x0, y0, x1, y1, 1, c);
}

void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
    if (w <= 0 || h <= 0) return;
    if (r <= 0) { display::fillRect(x, y, w, h, c); return; }
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    display::fillRect(x + r, y, w - 2 * r, h, c);
    display::fillRect(x, y + r, r, h - 2 * r, c);
    display::fillRect(x + w - r, y + r, r, h - 2 * r, c);

    for (int dy = 0; dy < r; dy++) {
        int dx = r - (int)(sqrtf((float)(r * r - (r - dy) * (r - dy))) + 0.5f);
        display::fillRect(x + dx, y + dy, w - 2 * dx, 1, c);
        display::fillRect(x + dx, y + h - 1 - dy, w - 2 * dx, 1, c);
    }
}

void roundRect(int x, int y, int w, int h, int r, uint16_t c) {
    if (r <= 0) { rect(x, y, w, h, c); return; }
    arc(x + r, y + r, r, 180, 270, c, 1);
    arc(x + w - r - 1, y + r, r, 270, 360, c, 1);
    arc(x + w - r - 1, y + h - r - 1, r, 0, 90, c, 1);
    arc(x + r, y + h - r - 1, r, 90, 180, c, 1);
    hline(x + r, y, w - 2 * r, c);
    hline(x + r, y + h - 1, w - 2 * r, c);
    vline(x, y + r, h - 2 * r, c);
    vline(x + w - 1, y + r, h - 2 * r, c);
}

void circle(int cx, int cy, int r, uint16_t c) {
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        px(cx + x, cy + y, c); px(cx + y, cy + x, c);
        px(cx - y, cy + x, c); px(cx - x, cy + y, c);
        px(cx - x, cy - y, c); px(cx - y, cy - x, c);
        px(cx + y, cy - x, c); px(cx + x, cy - y, c);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void fillCircle(int cx, int cy, int r, uint16_t c) {
    if (r <= 0) { px(cx, cy, c); return; }
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        hline(cx - x, cy + y, 2 * x + 1, c);
        hline(cx - x, cy - y, 2 * x + 1, c);
        hline(cx - y, cy + x, 2 * y + 1, c);
        hline(cx - y, cy - x, 2 * y + 1, c);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void arc(int cx, int cy, int r, int a0, int a1, uint16_t c, int thick) {
    if (a1 < a0) { int t = a0; a0 = a1; a1 = t; }
    int steps = (a1 - a0) / 3;
    if (steps < 12) steps = 12;
    for (int i = 0; i <= steps; i++) {
        float a = (a0 + (a1 - a0) * (float)i / (float)steps) * (float)DEG_TO_RAD;
        int x = cx + (int)lroundf(cosf(a) * r);
        int y = cy + (int)lroundf(sinf(a) * r);
        if (thick <= 1) px(x, y, c);
        else fillCircle(x, y, thick / 2, c);
    }
}

void triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c,
              bool fill_) {
    if (!fill_) {
        line(x0, y0, x1, y1, c);
        line(x1, y1, x2, y2, c);
        line(x2, y2, x0, y0, c);
        return;
    }
    int xs[3] = {x0, x1, x2};
    int ys[3] = {y0, y1, y2};
    int ymin = min(ys[0], min(ys[1], ys[2]));
    int ymax = max(ys[0], max(ys[1], ys[2]));
    for (int y = ymin; y <= ymax; y++) {
        int l = 32767, r = -32768;
        for (int i = 0; i < 3; i++) {
            int j = (i + 1) % 3;
            if ((ys[i] <= y && ys[j] >= y) || (ys[j] <= y && ys[i] >= y)) {
                int yy = ys[j] - ys[i];
                int xx = yy != 0 ? xs[i] + (y - ys[i]) * (xs[j] - xs[i]) / yy : xs[i];
                if (xx < l) l = xx;
                if (xx > r) r = xx;
            }
        }
        if (l <= r) hline(l, y, r - l + 1, c);
    }
}

void gradientV(int x, int y, int w, int h, uint16_t top, uint16_t bottom) {
    if (h <= 0) return;
    int r0 = (top >> 11) & 0x1F, g0 = (top >> 5) & 0x3F, b0 = top & 0x1F;
    int r1 = (bottom >> 11) & 0x1F, g1 = (bottom >> 5) & 0x3F, b1 = bottom & 0x1F;
    for (int i = 0; i < h; i++) {
        int r = r0 + (r1 - r0) * i / h;
        int g = g0 + (g1 - g0) * i / h;
        int b = b0 + (b1 - b0) * i / h;
        display::fillRect(x, y + i, w, 1,
                          (uint16_t)((r << 11) | (g << 5) | b));
    }
}

void progressBar(int x, int y, int w, int h, int value, int vmax,
                 uint16_t fg, uint16_t bg) {
    if (vmax <= 0) vmax = 1;
    if (value < 0) value = 0;
    if (value > vmax) value = vmax;
    fillRoundRect(x, y, w, h, h / 2, bg);
    int fillw = (int)((int64_t)w * value / vmax);
    if (fillw > 2) {
        if (fillw > w) fillw = w;
        fillRoundRect(x, y, fillw, h, h / 2, fg);
    }
}

void batteryIcon(int x, int y, int w, int h, int percent, uint16_t fg,
                 uint16_t bg) {
    (void)bg;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint16_t c = fg;
    if (percent <= 15) c = col::ERR;
    else if (percent <= 30) c = col::WARN;
    roundRect(x, y, w - 2, h, 2, c);
    fillRect(x + w - 2, y + h / 3, 2, h / 3, c);
    int inner = (w - 6) * percent / 100;
    if (inner > 0) fillRect(x + 2, y + 2, inner, h - 4, c);
}

int ascent(const GFXfont *f) {
    if (!f) return 8;
    if ((uint32_t)'H' >= f->first && (uint32_t)'H' <= f->last)
        return -(int)f->glyph[(uint32_t)'H' - f->first].yOffset;
    return (int)f->yAdvance - 6;
}

int textHeight(const GFXfont *f) { return f ? f->yAdvance : 8; }

int textWidth(const char *s, const GFXfont *f) {
    if (!s || !f) return 0;
    int w = 0;
    const char *p = s;
    while (*p) {
        uint32_t ch = utf8Next(&p);
        if (ch < f->first || ch > f->last) { w += f->yAdvance / 3; continue; }
        const GFXglyph *g = &f->glyph[ch - f->first];
        w += g->xAdvance;
    }
    return w;
}

void text(int x, int baseline, const char *s, const GFXfont *f, uint16_t fg,
          uint16_t bg) {
    if (!s || !f) return;
    int cx = x;
    const char *p = s;
    while (*p) {
        uint32_t ch = utf8Next(&p);
        if (ch == '\n') continue;
        if (ch >= f->first && ch <= f->last) {
            glyph(cx, baseline, ch, f, fg, bg);
            cx += f->glyph[ch - f->first].xAdvance;
        } else {
            cx += f->yAdvance / 3;
        }
    }
}

void textTop(int x, int top, const char *s, const GFXfont *f, uint16_t fg,
             uint16_t bg) {
    text(x, top + ascent(f), s, f, fg, bg);
}

void textCenter(int cx, int baseline, const char *s, const GFXfont *f,
                uint16_t fg, uint16_t bg) {
    text(cx - textWidth(s, f) / 2, baseline, s, f, fg, bg);
}

void textRight(int rx, int baseline, const char *s, const GFXfont *f,
               uint16_t fg, uint16_t bg) {
    text(rx - textWidth(s, f), baseline, s, f, fg, bg);
}

int textWidth5(const char *s, int scale) {
    if (!s) return 0;
    return (int)strlen(s) * 6 * scale;
}

int textHeight5(int scale) { return 8 * scale; }

void text5(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale) {
    if (!s) return;
    if (scale < 1) scale = 1;
    for (const char *p = s; *p; p++) {
        uint8_t ch = (uint8_t)*p;
        const uint8_t *g = &kFont5x7[ch * 5];
        for (int c = 0; c < 5; c++) {
            uint8_t bits = g[c];
            for (int r = 0; r < 8; r++) {
                uint16_t color = (bits & (1 << r)) ? fg : bg;
                if (scale == 1) px(x + c, y + r, color);
                else fillRect(x + c * scale, y + r * scale, scale, scale, color);
            }
        }

        if (scale == 1) vline(x + 5, y, 8, bg);
        else fillRect(x + 5 * scale, y, scale, 8 * scale, bg);
        x += 6 * scale;
    }
}

void drawIcon(Icon icon, int x, int y, int size, uint16_t color) {
    if (icon == Icon::None || size <= 2) return;
    const float k = size / 24.0f;
    auto X = [&](float v) { return x + (int)lroundf(v * k); };
    auto Y = [&](float v) { return y + (int)lroundf(v * k); };
    auto R = [&](float v) { return max(1, (int)lroundf(v * k)); };
    auto L = [&](float x0, float y0, float x1, float y1, int th = 2) {
        thickLine(X(x0), Y(y0), X(x1), Y(y1), max(1, (int)lroundf(th * k)), color);
    };
    auto FC = [&](float cx, float cy, float r) {
        fillCircle(X(cx), Y(cy), R(r), color);
    };
    auto FRC = [&](float rx, float ry, float rw, float rh, float rr) {
        fillRoundRect(X(rx), Y(ry), max(1, (int)lroundf(rw * k)),
                      max(1, (int)lroundf(rh * k)), max(1, (int)lroundf(rr * k)), color);
    };

    switch (icon) {
    case Icon::Wifi:
        arc(X(12), Y(19), R(4), 205, 335, color, R(1.6f));
        arc(X(12), Y(19), R(8), 210, 330, color, R(1.6f));
        arc(X(12), Y(19), R(12), 214, 326, color, R(1.6f));
        FC(12, 19.5f, 1.6f);
        break;
    case Icon::Bluetooth:
        L(12, 2.5f, 12, 21.5f);
        L(12, 2.5f, 18, 7.5f);
        L(18, 7.5f, 6.5f, 17);
        L(12, 12.5f, 18, 16.5f);
        L(18, 16.5f, 12, 21.5f);
        break;
    case Icon::SubGhz:
        L(12, 21, 12, 12);
        L(8, 22.5f, 12, 21);
        L(16, 22.5f, 12, 21);
        FC(12, 11, 1.8f);
        arc(X(12), Y(11), R(5), 200, 340, color, R(1.4f));
        arc(X(12), Y(11), R(9), 205, 335, color, R(1.4f));
        break;
    case Icon::Nrf:
        circle(X(12), Y(12), R(10), color);
        circle(X(12), Y(12), R(6), color);
        FC(12, 12, 2.2f);
        break;
    case Icon::Nfc:
        roundRect(X(2), Y(5), R(13), R(14), R(2), color);
        arc(X(15), Y(12), R(6), 285, 75, color, R(1.4f));
        arc(X(15), Y(12), R(10), 300, 60, color, R(1.4f));
        break;
    case Icon::Ir: {
        roundRect(X(8), Y(2), R(8), R(20), R(2), color);
        FC(8 + 4, 3.4f + 2.5f, 1.4f);
        FC(8 + 4, 3.4f + 7.5f, 1.4f);
        FC(8 + 4, 3.4f + 12.5f, 1.4f);
        L(19, 5, 22, 2);
        L(20, 9, 23.5f, 7.5f);
        break;
    }
    case Icon::Gps:
        circle(X(12), Y(12), R(7), color);
        L(12, 1, 12, 5); L(12, 19, 12, 23);
        L(1, 12, 5, 12); L(19, 12, 23, 12);
        FC(12, 12, 1.7f);
        break;
    case Icon::Game:
        FRC(1.5f, 7, 21, 11, 3.5f);
        fillRect(X(4), Y(11), R(5), R(2), col::BLACK);
        fillRect(X(5.5f), Y(9.5f), R(2), R(5), col::BLACK);
        FC(17, 10.5f, 1.4f);
        FC(19.5f, 13, 1.4f);
        break;
    case Icon::Folder:
        FRC(2, 6, 20, 14, 2.5f);
        fillRect(X(3), Y(4), R(8), R(4), color);
        fillRect(X(6), Y(10), R(12), R(1.2f), col::BLACK);
        break;
    case Icon::Chip:
        FRC(5, 5, 14, 14, 2);
        fillRect(X(8), Y(8), R(8), R(8), col::BLACK);
        for (int i = 0; i < 3; i++) {
            float o = 7.5f + i * 4.5f;
            L(5, o, 1.5f, o, 1.6f);
            L(19, o, 22.5f, o, 1.6f);
            L(o, 5, o, 1.5f, 1.6f);
            L(o, 19, o, 22.5f, 1.6f);
        }
        break;
    case Icon::Gear:
        circle(X(12), Y(12), R(8), color);
        for (int i = 0; i < 8; i++) {
            float a = i * 45.0f * (float)DEG_TO_RAD;
            L(12 + cosf(a) * 8, 12 + sinf(a) * 8, 12 + cosf(a) * 11.5f,
              12 + sinf(a) * 11.5f, 3);
        }
        fillCircle(X(12), Y(12), R(8), col::BLACK);
        circle(X(12), Y(12), R(4), color);
        break;
    case Icon::Info:
        circle(X(12), Y(12), R(9.5f), color);
        FC(12, 6.5f, 1.7f);
        fillRect(X(11), Y(10), R(2), R(8), color);
        break;
    case Icon::Battery:
        batteryIcon(X(1), Y(8), R(20), R(9), 100, color, col::BLACK);
        break;
    case Icon::Sd:
        FRC(5, 2, 14, 20, 2);
        triangle(X(9), Y(2), X(15), Y(2), X(15), Y(7), col::BLACK);
        fillRect(X(9), Y(2), R(3), R(2), col::BLACK);
        fillRect(X(14), Y(11), R(3), R(8), col::BLACK);
        break;
    case Icon::Terminal:
        roundRect(X(1.5f), Y(3.5f), R(21), R(17), R(2), color);
        L(6, 9, 9.5f, 12); L(9.5f, 12, 6, 15);
        L(12, 16, 18, 16);
        break;
    case Icon::Radar:
        circle(X(12), Y(12), R(10), color);
        circle(X(12), Y(12), R(5), color);
        L(12, 12, 20.5f, 5);
        FC(8, 9, 1.3f);
        FC(16, 16, 1.3f);
        break;
    case Icon::Clock:
        circle(X(12), Y(12), R(9.5f), color);
        L(12, 12, 12, 6, 1.8f);
        L(12, 12, 16, 14, 1.8f);
        break;
    case Icon::Speaker:
        triangle(X(3), Y(9), X(3), Y(15), X(9), Y(20), color);
        fillRect(X(3), Y(9), R(6), R(6), color);
        arc(X(9), Y(12), R(6), 300, 60, color, R(1.5f));
        arc(X(9), Y(12), R(10), 310, 50, color, R(1.5f));
        break;
    case Icon::Signal:
        for (int i = 0; i < 4; i++)
            fillRect(X(3 + i * 5), Y(20 - i * 4.5f), R(3), R(4.5f + i * 4.5f), color);
        break;
    case Icon::Lock:
        FRC(4.5f, 11, 15, 11, 2);
        arc(X(12), Y(11), R(5), 180, 360, color, R(2));
        fillCircle(X(12), Y(16), R(1.6f), col::BLACK);
        break;
    case Icon::Search:
        circle(X(10.5f), Y(10.5f), R(7), color);
        circle(X(10.5f), Y(10.5f), R(5.5f), col::BLACK);
        L(15.5f, 15.5f, 21, 21, 2.4f);
        break;
    case Icon::Play:
        triangle(X(7), Y(4), X(7), Y(20), X(20), Y(12), color);
        break;
    case Icon::Trash:
        fillRect(X(5), Y(6), R(14), R(2), color);
        roundRect(X(6.5f), Y(9), R(11), R(12), R(1.5f), color);
        fillRect(X(9), Y(12), R(6), R(7), col::BLACK);
        fillRect(X(10), Y(2.5f), R(4), R(3), color);
        break;
    case Icon::Map:
        rect(X(2), Y(5), R(6), R(15), color);
        rect(X(9), Y(3), R(6), R(17), color);
        rect(X(16), Y(5), R(6), R(15), color);
        L(9, 3, 9, 20); L(16, 5, 16, 20);
        break;
    case Icon::Usb:
        roundRect(X(8), Y(2), R(8), R(6), R(1), color);
        L(12, 8, 12, 21);
        fillRect(X(9), Y(12), R(6), R(3), color);
        L(12, 18, 8, 15); L(12, 20, 16, 17);
        break;
    case Icon::Web:
        circle(X(12), Y(12), R(10), color);
        L(2, 12, 22, 12);
        arc(X(12), Y(12), R(10), 250, 290, color, 1);
        arc(X(12), Y(12), R(10), 70, 110, color, 1);
        arc(X(12), Y(12), R(4), 0, 360, color, 1);
        break;
    case Icon::Log:
        fillRect(X(4), Y(4), R(14), R(2), color);
        fillRect(X(4), Y(9), R(16), R(2), color);
        fillRect(X(4), Y(14), R(12), R(2), color);
        fillRect(X(4), Y(19), R(9), R(2), color);
        break;
    case Icon::Star:
        for (int i = 0; i < 5; i++) {
            float a0 = (-90 + i * 72) * (float)DEG_TO_RAD;
            float a1 = (-90 + i * 72 + 36) * (float)DEG_TO_RAD;
            triangle(X(12), Y(12), X(12 + cosf(a0) * 11), Y(12 + sinf(a0) * 11),
                     X(12 + cosf(a1) * 5), Y(12 + sinf(a1) * 5), color);
        }
        break;
    case Icon::Crosshair:
        circle(X(12), Y(12), R(6), color);
        L(12, 1, 12, 6); L(12, 18, 12, 23);
        L(1, 12, 6, 12); L(18, 12, 23, 12);
        break;
    case Icon::ArrowUp:
        triangle(X(12), Y(3), X(3), Y(14), X(21), Y(14), color);
        fillRect(X(9.5f), Y(13), R(5), R(8), color);
        break;
    case Icon::ArrowDown:
        triangle(X(12), Y(21), X(3), Y(10), X(21), Y(10), color);
        fillRect(X(9.5f), Y(3), R(5), R(8), color);
        break;
    case Icon::Warning:
        triangle(X(12), Y(2), X(1), Y(21), X(23), Y(21), color);
        fillRect(X(10.5f), Y(8), R(3), R(8), col::BLACK);
        fillRect(X(10.5f), Y(17.5f), R(3), R(2.5f), col::BLACK);
        break;
    case Icon::Check:
        L(3, 12, 9, 18, 3);
        L(9, 18, 21, 5, 3);
        break;
    case Icon::Key:
        circle(X(8), Y(8), R(4), color);
        circle(X(8), Y(8), R(1.6f), col::BLACK);
        L(11, 11, 21, 21, 2.4f);
        L(16, 16, 19, 13, 2.2f);
        L(19, 19, 22, 16, 2.2f);
        break;
    default:
        break;
    }
}

void ellipsize(char *dst, size_t dstSize, const char *src, int maxPx,
               const GFXfont *f) {
    if (!dst || dstSize == 0) return;
    if (!src) { dst[0] = 0; return; }
    if (textWidth(src, f) <= maxPx) {
        strncpy(dst, src, dstSize - 1);
        dst[dstSize - 1] = 0;
        return;
    }
    size_t len = strlen(src);
    if (len > dstSize - 1) len = dstSize - 1;
    while (len > 0) {
        memcpy(dst, src, len);
        dst[len] = 0;
        char probe[80];
        snprintf(probe, sizeof(probe), "%s..", dst);
        if (textWidth(probe, f) <= maxPx) {
            snprintf(dst, dstSize, "%s..", dst);
            return;
        }
        len--;
    }
    dst[0] = 0;
}

}
