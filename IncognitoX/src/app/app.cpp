#include "app/app.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app/settings.h"
#include "config.h"
#include "hw/buzzer.h"
#include "hw/display.h"
#include "hw/gfx.h"
#include "hw/power.h"
#include "hw/storage.h"
#include "app/log.h"

namespace app {
namespace {

constexpr int kMaxDepth = 8;
Screen *s_stack[kMaxDepth];
int     s_depth = 0;

bool     s_invalidate = true;
bool     s_chromeDirty = true;
bool     s_statusDirty = true;
uint32_t s_lastStatus = 0;

char     s_toast[64] = {0};
uint32_t s_toastUntil = 0;
int      s_toastY = 0;

char     s_lastTitle[48] = {0};
char     s_lastHint[96] = {0};

void drawStatusBar() {
    if (s_depth <= 0) return;
    const uint16_t bg = gfx::col::BG2;
    gfx::fillRect(0, 0, UI_W, UI_STATUS_H, bg);

    const char *t = s_stack[s_depth - 1] ? s_stack[s_depth - 1]->title() : "";
    char title[40];
    gfx::ellipsize(title, sizeof(title), t, 104, gfx::F_SMALL_B);
    gfx::textTop(8, 5, title, gfx::F_SMALL_B, gfx::col::accent, bg);

    int x = UI_W - 8;
    if (storage::mounted()) {
        gfx::drawIcon(gfx::Icon::Sd, x - 14, 6, 13, gfx::col::DIM);
    }
    x -= 20;

    int pct = power::percent();
    if (pct >= 0) {
        gfx::batteryIcon(x - 24, 8, 24, 11, pct, gfx::col::TEXT2, bg);
        char b[8];
        snprintf(b, sizeof(b), "%d%%", pct);
        gfx::textRight(x - 28, 19, b, gfx::F_SMALL, gfx::col::TEXT2, bg);
        x -= 28 + gfx::textWidth(b, gfx::F_SMALL) + 4;
    } else {
        gfx::drawIcon(gfx::Icon::Battery, x - 20, 5, 16, gfx::col::DIM2);
        x -= 24;
    }

    time_t now = time(nullptr) + (time_t)settings.tz * 3600;
    struct tm tmv;
    gmtime_r(&now, &tmv);
    char clock[8] = "--:--";
    if (tmv.tm_year > 120) snprintf(clock, sizeof(clock), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    gfx::textRight(x - 4, 19, clock, gfx::F_SMALL, gfx::col::TEXT2, bg);

    gfx::hline(0, UI_STATUS_H - 1, UI_W, gfx::col::accent);
}

void drawHintBar() {
    if (s_depth <= 0) return;
    const uint16_t bg = gfx::col::BG2;
    gfx::hline(0, UI_CONTENT_Y1, UI_W, gfx::col::LINE);
    gfx::fillRect(0, UI_CONTENT_Y1 + 1, UI_W, UI_HINT_H - 1, bg);
    Screen *s = s_stack[s_depth - 1];
    const char *h = s ? s->hint() : "";
    char buf[96];
    gfx::ellipsize(buf, sizeof(buf), h, UI_W - 20, gfx::F_SMALL);
    gfx::text(8, UI_CONTENT_Y1 + 18, buf, gfx::F_SMALL, gfx::col::DIM, bg);
}

void drawChrome() {
    if (s_depth <= 0) return;
    Screen *s = s_stack[s_depth - 1];
    if (!s) return;
    if (!s->chrome()) return;
    drawStatusBar();
    drawHintBar();
    s_chromeDirty = false;
    strncpy(s_lastTitle, s->title(), sizeof(s_lastTitle) - 1);
    strncpy(s_lastHint, s->hint(), sizeof(s_lastHint) - 1);
}

void drawToastOverlay() {
    if (!s_toast[0]) return;
    int w = gfx::textWidth(s_toast, gfx::F_SMALL) + 24;
    if (w > UI_W - 16) w = UI_W - 16;
    int x = (UI_W - w) / 2;
    int y = UI_CONTENT_Y1 - 34;
    s_toastY = y;
    gfx::fillRoundRect(x, y, w, 26, 6, gfx::col::PANEL2);
    gfx::roundRect(x, y, w, 26, 6, gfx::col::accent);
    gfx::textCenter(UI_W / 2, y + 18, s_toast, gfx::F_SMALL, gfx::col::TEXT,
                    gfx::col::PANEL2);
}

void redrawAll() {
    if (s_depth <= 0) return;
    Screen *s = s_stack[s_depth - 1];
    if (!s) return;
    if (s->chrome()) {
        drawChrome();
        gfx::fillRect(0, UI_CONTENT_Y0, UI_W, UI_CONTENT_Y1 - UI_CONTENT_Y0,
                      gfx::col::BG);
    } else {
        gfx::fillRect(0, 0, UI_W, 320, gfx::col::BG);
    }
    s->draw();
    if (s_toast[0]) drawToastOverlay();
    s_invalidate = false;
}

}

int contentTop() { return UI_CONTENT_Y0; }
int contentBottom() { return UI_CONTENT_Y1; }
int contentHeight() { return UI_CONTENT_Y1 - UI_CONTENT_Y0; }

void begin() {
    s_depth = 0;
    s_invalidate = true;
    s_chromeDirty = true;
}

void push(Screen *s) {
    if (!s) return;
    if (s_depth >= kMaxDepth) { delete s; return; }
    s_stack[s_depth++] = s;
    s->enter();
    s_invalidate = true;
    s_chromeDirty = true;
}

void pop() {
    if (s_depth <= 1) return;
    Screen *s = s_stack[--s_depth];
    s->leave();
    delete s;
    s_stack[s_depth]->resume();
    s_invalidate = true;
    s_chromeDirty = true;
}

void popToRoot() {
    while (s_depth > 1) pop();
}

void invalidate() { s_invalidate = true; }
Screen *current() { return s_depth ? s_stack[s_depth - 1] : nullptr; }
int depth() { return s_depth; }
void setStatusDirty() { s_statusDirty = true; }

void toast(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_toast, sizeof(s_toast), fmt, ap);
    va_end(ap);
    s_toastUntil = millis() + TOAST_MS;
    s_invalidate = true;
}

void loopOnce() {

    static bool s_sleeping = false;
    if (!s_sleeping && settings.autosleep > 0 &&
        input.idleMs() > (uint32_t)settings.autosleep * 60000UL) {
        display::sleep(true);
        s_sleeping = true;
        klog::add("screen sleep");
    }
    if (s_sleeping) {
        input.poll();
        if (input.anyDown()) {
            display::sleep(false);
            s_sleeping = false;
            Key dk;
            KeyEvent de;
            while (input.pop(dk, de)) {}
            s_invalidate = true;
            s_statusDirty = true;
        }
        buzzer::tick();
        power::tick();
        delay(20);
        return;
    }

    input.poll();

    Key k;
    KeyEvent e;
    while (input.pop(k, e)) {
        Screen *s = s_stack[s_depth - 1];
        if (!s) continue;
        if (e == KeyEvent::Press && settings.sound) buzzer::click();
        bool handled = s->onKey(k, e);
        if (handled) {
            s_invalidate = true;
        } else if (k == Key::Left && (e == KeyEvent::Press || e == KeyEvent::Repeat)) {
            if (s_depth > 1) {
                pop();
            }
        }
    }

    if (s_depth) s_stack[s_depth - 1]->tick();
    buzzer::tick();
    power::tick();

    if (s_toast[0] && (int32_t)(millis() - s_toastUntil) >= 0) {
        s_toast[0] = 0;
        s_invalidate = true;
    }

    Screen *s = s_stack[s_depth - 1];
    if (s && s->chrome()) {
        if (strncmp(s_lastTitle, s->title(), sizeof(s_lastTitle) - 1) != 0 ||
            strncmp(s_lastHint, s->hint(), sizeof(s_lastHint) - 1) != 0) {
            s_chromeDirty = true;
            s_invalidate = true;
        }
    }

    if (millis() - s_lastStatus > 1000) {
        s_lastStatus = millis();
        s_statusDirty = true;
    }
    if (s_invalidate || s_chromeDirty) {
        redrawAll();
        s_statusDirty = false;
    } else if (s_statusDirty && s && s->chrome()) {
        drawStatusBar();
        s_statusDirty = false;
    }
}

static void dialogFrame(const char *title, const char *text, const char *keys) {
    const int x = 14, y = 42, w = 212, h = 148;
    gfx::fillRoundRect(x, y, w, h, 8, gfx::col::PANEL);
    gfx::roundRect(x, y, w, h, 8, gfx::col::accent);
    gfx::textCenter(120, y + 28, title, gfx::F_BODY_B, gfx::col::accent,
                    gfx::col::PANEL);
    gfx::hline(x + 12, y + 38, w - 24, gfx::col::LINE);

    char buf[160];
    strncpy(buf, text ? text : "", sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    int maxW = w - 24;
    char lineBuf[64] = {0};
    int ly = y + 62;
    char *p = buf;
    while (*p && ly < y + h - 38) {
        char word[40] = {0};
        int i = 0;
        while (*p == ' ') p++;
        while (*p && *p != ' ' && i < 39) word[i++] = *p++;
        word[i] = 0;
        char probe[96];
        snprintf(probe, sizeof(probe), "%s%s%s", lineBuf, lineBuf[0] ? " " : "", word);
        if (gfx::textWidth(probe, gfx::F_SMALL) > maxW && lineBuf[0]) {
            gfx::text(x + 12, ly, lineBuf, gfx::F_SMALL, gfx::col::TEXT2,
                      gfx::col::PANEL);
            ly += 16;
            strncpy(lineBuf, word, sizeof(lineBuf) - 1);
        } else {
            strncpy(lineBuf, probe, sizeof(lineBuf) - 1);
        }
    }
    if (lineBuf[0])
        gfx::text(x + 12, ly, lineBuf, gfx::F_SMALL, gfx::col::TEXT2,
                  gfx::col::PANEL);
    gfx::textCenter(120, y + h - 12, keys, gfx::F_SMALL, gfx::col::DIM,
                    gfx::col::PANEL);
}

bool confirm(const char *title, const char *text) {
    dialogFrame(title, text, "OK = да      < = нет");
    for (;;) {
        input.poll();
        buzzer::tick();
        Key k;
        KeyEvent e;
        while (input.pop(k, e)) {
            if (e == KeyEvent::Press || e == KeyEvent::Repeat) {
                if (k == Key::Ok) {
                    buzzer::ok();
                    invalidate();
                    return true;
                }
                if (k == Key::Left || k == Key::Down) {
                    buzzer::click();
                    invalidate();
                    return false;
                }
            }
        }
        delay(8);
    }
}

void message(const char *title, const char *text) {
    dialogFrame(title, text, "OK = продолжить");
    for (;;) {
        input.poll();
        buzzer::tick();
        Key k;
        KeyEvent e;
        while (input.pop(k, e)) {
            if ((e == KeyEvent::Press || e == KeyEvent::Repeat) && k == Key::Ok) {
                buzzer::click();
                invalidate();
                return;
            }
        }
        delay(8);
    }
}

void error(const char *title, const char *text) {
    buzzer::err();
    message(title, text);
}

}
