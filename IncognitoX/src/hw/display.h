#pragma once

#include <Arduino.h>

namespace display {

void    init();
int     width();
int     height();

void    fillScreen(uint16_t color);
void    fillRect(int x, int y, int w, int h, uint16_t color);
void    pushImage(int x, int y, int w, int h, const uint16_t *pixels);
void    pushImageMono(int x, int y, int w, int h, const uint8_t *bits,
                      uint16_t fg, uint16_t bg);

void    setAddrWindow(int x, int y, int w, int h);
void    beginPixels();
void    pushPixels(const uint16_t *pixels, size_t count);
void    endPixels();

void    backlight(uint8_t percent);
uint8_t backlight();
void    sleep(bool on);
void    invert(bool on);
void    setRotation(uint8_t r);

}
