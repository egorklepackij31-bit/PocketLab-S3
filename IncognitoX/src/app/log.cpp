#include "app/log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "hw/storage.h"

namespace klog {
namespace {

char     s_lines[LOG_LINES][LOG_MAXLEN];
uint8_t  s_head = 0;
uint8_t  s_count = 0;
bool     s_sd = true;
char     s_path[40] = {0};

}

void begin() {
    s_head = s_count = 0;
    s_sd = storage::mounted();
    time_t now = time(nullptr);
    struct tm tmv;
    gmtime_r(&now, &tmv);
    if (tmv.tm_year > 120) {
        snprintf(s_path, sizeof(s_path), LOG_DIR "/session_%04d%02d%02d_%02d%02d.log",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour,
                 tmv.tm_min);
    } else {
        snprintf(s_path, sizeof(s_path), LOG_DIR "/session_boot.log");
    }
}

void addLine(const char *text) {
    if (!text) return;
    uint32_t ms = millis();
    snprintf(s_lines[s_head], LOG_MAXLEN, "[%lu.%02lu] %s",
             (unsigned long)(ms / 1000), (unsigned long)((ms % 1000) / 10), text);
    s_head = (uint8_t)((s_head + 1) % LOG_LINES);
    if (s_count < LOG_LINES) s_count++;

    if (s_sd && storage::mounted() && s_path[0]) {
        storage::appendLine(s_path, s_lines[(s_head + LOG_LINES - 1) % LOG_LINES]);
    }
    Serial.println(s_lines[(s_head + LOG_LINES - 1) % LOG_LINES]);
}

void add(const char *fmt, ...) {
    char buf[LOG_MAXLEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    addLine(buf);
}

int count() { return s_count; }

const char *line(int i) {
    if (i < 0 || i >= s_count) return "";
    int idx = (s_head + LOG_LINES - s_count + i + LOG_LINES) % LOG_LINES;
    return s_lines[idx];
}

void clear() {
    s_head = s_count = 0;
}

void setToSd(bool on) { s_sd = on; }
bool toSd() { return s_sd; }
const char *sessionPath() { return s_path; }

}
