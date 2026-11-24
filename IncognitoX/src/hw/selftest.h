#pragma once

#include <Arduino.h>

namespace selftest {

enum Status : uint8_t { ST_OK = 0, ST_WARN = 1, ST_FAIL = 2 };

struct Item {
    const char *name;
    Status      status;
    char        detail[40];
};

int          run();
int          count();
const Item  &at(int i);
int          failures();
uint32_t     lastRunMs();

}
