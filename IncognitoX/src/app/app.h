#pragma once

#include <Arduino.h>

#include "hw/input.h"

class Screen {
public:
    virtual ~Screen() {}
    virtual const char *title() const = 0;
    virtual void enter() {}
    virtual void leave() {}
    virtual void resume() {}
    virtual void draw() = 0;
    virtual void tick() {}
    virtual bool onKey(Key k, KeyEvent e) = 0;
    virtual bool chrome() const { return true; }
    virtual const char *hint() const { return "< back    OK select"; }
};

namespace app {

void begin();
void loopOnce();

void push(Screen *s);
void pop();
void popToRoot();
void invalidate();

Screen *current();
int     depth();

void toast(const char *fmt, ...);
void setStatusDirty();

bool confirm(const char *title, const char *text);
void message(const char *title, const char *text);
void error(const char *title, const char *text);

int  contentTop();
int  contentBottom();
int  contentHeight();

}
