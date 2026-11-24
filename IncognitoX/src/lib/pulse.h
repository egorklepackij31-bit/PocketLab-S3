#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct OokCode {
    bool     valid = false;
    uint8_t  bits = 0;
    uint32_t value = 0;
    uint8_t  frames = 0;
    uint32_t shortUs = 0;
    uint32_t longUs = 0;
    char     hex[12] = {0};
    char     chips[40] = {0};
};

OokCode ookDecode(const uint32_t *durations, int count);
