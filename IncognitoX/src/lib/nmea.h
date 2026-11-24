#pragma once

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

struct GpsFix {
    bool     valid = false;
    double   lat = 0.0;
    double   lon = 0.0;
    float    alt = 0.0f;
    float    hdop = 0.0f;
    float    speedKmh = 0.0f;
    float    course = 0.0f;
    uint8_t  sats = 0;
    uint8_t  satsInView = 0;
    uint8_t  fixQuality = 0;
    uint8_t  hour = 0, minute = 0, second = 0;
    uint8_t  day = 0, month = 0;
    uint16_t year = 0;
    uint32_t sentences = 0;
    uint32_t lastFixMs = 0;
    char     talker[4] = {0};
};

class NmeaParser {
public:
    void feed(char c);
    void reset() { len_ = 0; }
    const GpsFix &fix() const { return fix_; }
    GpsFix &mutableFix() { return fix_; }

    void feedLine(const char *line);

private:
    char    buf_[128];
    uint8_t len_ = 0;
    GpsFix  fix_;

    void parse(char *line);
    static double coord(const char *val, const char *hemi);
    static float  fnum(const char *s);
    static int    inum(const char *s);
};

bool nmeaChecksumOk(const char *line);
