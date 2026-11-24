#include "lib/nmea.h"

namespace {

char *nth(char *line, int idx) {
    int cur = 0;
    char *p = line;
    if (idx == 0) return p;
    while (*p) {
        if (*p == ',') {
            cur++;
            if (cur == idx) return p + 1;
        }
        p++;
    }
    return nullptr;
}

void cutField(char *line, int idx, char *out, size_t outSize) {
    char *start = nth(line, idx);
    out[0] = 0;
    if (!start) return;
    size_t i = 0;
    while (start[i] && start[i] != ',' && start[i] != '*' && i < outSize - 1) {
        out[i] = start[i];
        i++;
    }
    out[i] = 0;
}

}

bool nmeaChecksumOk(const char *line) {
    if (!line || line[0] != '$') return false;
    const char *star = strchr(line, '*');
    if (!star) return false;
    uint8_t sum = 0;
    for (const char *p = line + 1; p < star; p++) sum ^= (uint8_t)*p;
    uint8_t want = (uint8_t)strtoul(star + 1, nullptr, 16);
    return sum == want;
}

double NmeaParser::coord(const char *val, const char *hemi) {
    if (!val || !*val) return 0.0;
    double raw = atof(val);
    int deg = (int)(raw / 100.0);
    double min = raw - deg * 100.0;
    double res = deg + min / 60.0;
    if (hemi && (*hemi == 'S' || *hemi == 'W')) res = -res;
    return res;
}

float NmeaParser::fnum(const char *s) { return (s && *s) ? (float)atof(s) : 0.0f; }
int   NmeaParser::inum(const char *s) { return (s && *s) ? atoi(s) : 0; }

void NmeaParser::feed(char c) {
    if (c == '$') { len_ = 0; buf_[len_++] = c; return; }
    if (len_ == 0) return;
    if (c == '\n') {
        if (len_ > 0) {
            buf_[len_] = 0;
            parse(buf_);
        }
        len_ = 0;
        return;
    }
    if (c == '\r') return;
    if (len_ < sizeof(buf_) - 1) buf_[len_++] = c;
}

void NmeaParser::feedLine(const char *line) {
    if (!line) return;
    size_t n = strlen(line);
    for (size_t i = 0; i < n; i++) feed(line[i]);
    feed('\n');
}

void NmeaParser::parse(char *line) {
    if (!line || line[0] != '$') return;
    if (line[1] == 0) return;

    if (strchr(line, '*') && !nmeaChecksumOk(line)) return;

    char *star = strchr(line, '*');
    if (star) *star = 0;

    if (strlen(line) < 7) return;
    strncpy(fix_.talker, line + 1, 2);
    fix_.talker[2] = 0;

    const char *type = line + 3;
    fix_.sentences++;

    char f[16];

    if (strncmp(type, "GGA", 3) == 0) {
        cutField(line, 1, f, sizeof(f));
        if (strlen(f) >= 6) {
            fix_.hour = (uint8_t)((f[0] - '0') * 10 + (f[1] - '0'));
            fix_.minute = (uint8_t)((f[2] - '0') * 10 + (f[3] - '0'));
            fix_.second = (uint8_t)((f[4] - '0') * 10 + (f[5] - '0'));
        }
        char lat[16], ns[4], lon[16], ew[4];
        cutField(line, 2, lat, sizeof(lat));
        cutField(line, 3, ns, sizeof(ns));
        cutField(line, 4, lon, sizeof(lon));
        cutField(line, 5, ew, sizeof(ew));
        cutField(line, 6, f, sizeof(f));
        fix_.fixQuality = (uint8_t)inum(f);
        cutField(line, 7, f, sizeof(f));
        fix_.sats = (uint8_t)inum(f);
        cutField(line, 8, f, sizeof(f));
        fix_.hdop = fnum(f);
        cutField(line, 9, f, sizeof(f));
        fix_.alt = fnum(f);

        if (lat[0] && lon[0]) {
            fix_.lat = coord(lat, ns);
            fix_.lon = coord(lon, ew);
        }
        fix_.valid = (fix_.fixQuality > 0);
    } else if (strncmp(type, "RMC", 3) == 0) {
        cutField(line, 1, f, sizeof(f));
        if (strlen(f) >= 6) {
            fix_.hour = (uint8_t)((f[0] - '0') * 10 + (f[1] - '0'));
            fix_.minute = (uint8_t)((f[2] - '0') * 10 + (f[3] - '0'));
            fix_.second = (uint8_t)((f[4] - '0') * 10 + (f[5] - '0'));
        }
        char status[4];
        cutField(line, 2, status, sizeof(status));
        char lat[16], ns[4], lon[16], ew[4];
        cutField(line, 3, lat, sizeof(lat));
        cutField(line, 4, ns, sizeof(ns));
        cutField(line, 5, lon, sizeof(lon));
        cutField(line, 6, ew, sizeof(ew));
        cutField(line, 7, f, sizeof(f));
        fix_.speedKmh = fnum(f) * 1.852f;
        cutField(line, 8, f, sizeof(f));
        fix_.course = fnum(f);
        cutField(line, 9, f, sizeof(f));
        if (strlen(f) == 6) {
            fix_.day = (uint8_t)((f[0] - '0') * 10 + (f[1] - '0'));
            fix_.month = (uint8_t)((f[2] - '0') * 10 + (f[3] - '0'));
            int yy = (f[4] - '0') * 10 + (f[5] - '0');
            fix_.year = (yy >= 70) ? (uint16_t)(1900 + yy) : (uint16_t)(2000 + yy);
        }
        if (lat[0] && lon[0]) {
            fix_.lat = coord(lat, ns);
            fix_.lon = coord(lon, ew);
        }
        if (status[0] == 'A') fix_.valid = true;
        else if (status[0] == 'V') fix_.valid = false;
    } else if (strncmp(type, "GSV", 3) == 0) {
        cutField(line, 3, f, sizeof(f));
        fix_.satsInView = (uint8_t)inum(f);
    } else if (strncmp(type, "GSA", 3) == 0) {
        cutField(line, 2, f, sizeof(f));
        if (inum(f) == 0) fix_.valid = false;
    }
}
