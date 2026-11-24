#include "lib/pulse.h"

OokCode ookDecode(const uint32_t *d, int n) {
    OokCode out;
    if (!d || n < 8) return out;

    uint32_t vals[64];
    int vn = 0;
    for (int i = 0; i < n && vn < 64; i++)
        if (d[i] > 80 && d[i] < 3000) vals[vn++] = d[i];
    if (vn < 4) return out;
    for (int i = 1; i < vn; i++) {
        uint32_t k = vals[i];
        int j = i - 1;
        while (j >= 0 && vals[j] > k) { vals[j + 1] = vals[j]; j--; }
        vals[j + 1] = k;
    }
    uint32_t base = vals[vn / 4];
    if (base < 80) base = 80;
    if (base > 10000) base = 10000;
    out.shortUs = base;
    out.longUs = (uint32_t)(base * 3);
    const uint32_t th = base + base / 2 + base / 4;

    int s = 0;
    for (int i = 0; i + 8 < n; i++) {
        if (d[i] > base * 6) { s = i + 1; break; }
    }
    if (n - s < 8) return out;

    uint32_t value = 0;
    uint8_t bits = 0;
    uint32_t frames = 0;
    char bitChars[40] = {0};
    int bitLen = 0;

    int i = s;
    while (i + 1 < n && bits < 32) {
        uint32_t gap = d[i + 1];
        if (gap > base * 6) { frames++; break; }
        bool one = gap >= th;
        if (one) value |= (1UL << bits);
        bits++;
        if (bitLen < 40) bitChars[bitLen++] = one ? '1' : '0';
        i += 2;
    }

    if (i < (int)n) {
        uint32_t lastGap = d[i];
        if (lastGap > base * 6) frames++;
    }

    if (bits < 8) return out;

    out.valid = true;
    out.bits = bits;
    out.value = value;
    snprintf(out.hex, sizeof(out.hex), "%0*X", (int)((bits + 3) / 4), (unsigned)value);

    char chips[40] = {0};
    int chipLen = 0;
    for (int j = 0; j + 1 < bitLen; j += 2) {
        char a = bitChars[j];
        char b = bitChars[j + 1];
        char c = ((a == '1') && (b == '1')) ? '1'
               : ((a == '0') && (b == '0')) ? '0'
               : 'F';
        if (chipLen < 39) chips[chipLen++] = c;
    }
    chips[chipLen] = 0;
    strncpy(out.chips, chips, sizeof(out.chips) - 1);
    out.chips[sizeof(out.chips) - 1] = 0;
    out.frames = frames;
    return out;
}
