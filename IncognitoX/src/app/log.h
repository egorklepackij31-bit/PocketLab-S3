#pragma once

#include <Arduino.h>

namespace klog {

void        begin();
void        add(const char *fmt, ...);
void        addLine(const char *line);

int         count();
const char *line(int i);
void        clear();

void        setToSd(bool on);
bool        toSd();
const char *sessionPath();

}
