#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "lib/nmea.h"
#include "lib/pulse.h"

static int g_fail = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            g_fail++;                                                      \
        }                                                                  \
    } while (0)

#define NEAR(a, b, eps) CHECK(std::fabs((a) - (b)) <= (eps))

static std::string sentence(const char *body) {
    unsigned char sum = 0;
    for (const char *p = body; *p; p++) sum ^= (unsigned char)*p;
    char buf[256];
    std::snprintf(buf, sizeof(buf), "$%s*%02X", body, sum);
    return std::string(buf);
}

static void testNmea() {
    NmeaParser p;
    p.feedLine(sentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,")
                   .c_str());
    const GpsFix &f = p.fix();
    CHECK(f.sentences == 1);
    CHECK(f.valid);
    CHECK(f.fixQuality == 1);
    CHECK(f.sats == 8);
    NEAR(f.hdop, 0.9, 0.05);
    NEAR(f.alt, 545.4, 0.1);
    NEAR(f.lat, 48.1173, 0.0005);
    NEAR(f.lon, 11.516666, 0.0005);
    CHECK(f.hour == 12 && f.minute == 35 && f.second == 19);
    CHECK(std::strcmp(f.talker, "GP") == 0);

    p.feedLine(sentence("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W")
                   .c_str());
    NEAR(f.speedKmh, 22.4 * 1.852, 0.05);
    NEAR(f.course, 84.4, 0.05);
    CHECK(f.day == 23 && f.month == 3 && f.year == 1994);
    CHECK(f.valid);

    p.feedLine(sentence("GPGSV,2,1,08,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,45")
                   .c_str());
    CHECK(f.satsInView == 8);

    uint32_t before = f.sentences;
    p.feedLine("$GPGGA,000000,0000.000,S,00000.000,W,0,00,99.9,0,M,0,M,,*00");
    CHECK(f.sentences == before);

    std::string good = sentence("GPRMC,123519,A,,,,,,,,");
    CHECK(nmeaChecksumOk(good.c_str()));
    good[good.size() - 1] = good[good.size() - 1] == '0' ? '1' : '0';
    CHECK(!nmeaChecksumOk(good.c_str()));

    NmeaParser q;
    q.feedLine(sentence("GPGGA,010203,3352.000,S,07030.000,W,2,05,1.1,100.0,M,0,M,,")
                   .c_str());
    NEAR(q.fix().lat, -33.866666, 0.0005);
    NEAR(q.fix().lon, -70.5, 0.0005);
}

static void testOok() {

    uint32_t d[64];
    int n = 0;
    const uint32_t value = 0xA5A5A5u;
    for (int bit = 0; bit < 24; bit++) {
        d[n++] = 300;
        d[n++] = (value & (1u << bit)) ? 900u : 300u;
    }
    d[n++] = 9000;

    OokCode c = ookDecode(d, n);
    CHECK(c.valid);
    CHECK(c.bits == 24);
    CHECK(c.value == value);
    CHECK(c.frames == 1);
    CHECK(c.shortUs >= 250 && c.shortUs <= 350);
    CHECK(std::strcmp(c.hex, "A5A5A5") == 0);
    CHECK(c.chips[0] != 0);
    std::printf("  OOK: %s, %u bits, short=%lu us, chips=%s\n", c.hex, c.bits,
                (unsigned long)c.shortUs, c.chips);

    uint32_t noise[6] = {100, 110, 95, 120, 105, 130};
    OokCode bad = ookDecode(noise, 6);
    CHECK(!bad.valid);

    uint32_t lead[48];
    int m = 0;
    lead[m++] = 9000;
    for (int i = 0; i < 20; i++) {
        lead[m++] = 300;
        lead[m++] = 300;
    }
    OokCode leadCode = ookDecode(lead, m);
    CHECK(!leadCode.valid || leadCode.bits >= 8);
}

int main() {
    std::printf("IncognitoX host tests\n");
    testNmea();
    testOok();
    if (g_fail == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d TEST(S) FAILED\n", g_fail);
    return 1;
}
