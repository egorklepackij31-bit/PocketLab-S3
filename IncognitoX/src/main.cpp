#include <Arduino.h>
#include <time.h>

#include "app/app.h"
#include "app/log.h"
#include "app/settings.h"
#include "app/ui.h"
#include "config.h"
#include "hw/buzzer.h"
#include "hw/display.h"
#include "hw/gfx.h"
#include "hw/gps.h"
#include "hw/input.h"
#include "hw/power.h"
#include "hw/selftest.h"
#include "hw/storage.h"
#include "lib/cc1101.h"
#include "lib/ir.h"
#include "lib/net.h"
#include "lib/nrf24.h"
#include "lib/pn532.h"
#include "lib/rc522.h"
#include "screens/screens.h"

namespace {

void drawBootLogo() {
    gfx::fillScreen(gfx::col::BLACK);
    gfx::gradientV(0, 0, 240, 120, gfx::col::rgb(40, 18, 0), gfx::col::BLACK);

    gfx::fillCircle(120, 70, 26, gfx::col::ORANGE);
    gfx::fillCircle(128, 63, 18, gfx::col::BLACK);
    gfx::triangle(128, 63, 172, 40, 158, 74, gfx::col::ORANGE);
    gfx::triangle(128, 63, 176, 54, 160, 82, gfx::col::accentDim());
    gfx::fillCircle(112, 78, 5, gfx::col::WARN);

    gfx::textCenter(120, 62, "IncognitoX", gfx::F_BIG_B, gfx::col::TEXT, gfx::col::BLACK);
    gfx::textCenter(120, 88, "S3  MULTI-TOOL", gfx::F_SMALL, gfx::col::accent,
              gfx::col::BLACK);
    gfx::hline(54, 98, 132, gfx::col::accent);
    gfx::textCenter(120, 116, "v" IX_VERSION_STR "   ESP32-S3 N16R8", gfx::F_SMALL,
              gfx::col::DIM, gfx::col::BLACK);
}

void drawBootItem(int idx, const selftest::Item &it, int y) {
    uint16_t c = it.status == selftest::ST_OK      ? gfx::col::OK
                 : it.status == selftest::ST_WARN  ? gfx::col::WARN
                                                   : gfx::col::ERR;
    gfx::fillRect(8, y - 1, 224, 8, gfx::col::BLACK);
    gfx::drawIcon(it.status == selftest::ST_OK ? gfx::Icon::Check
                                               : gfx::Icon::Warning,
                  8, y - 1, 8, c);
    gfx::text5(22, y, it.name, gfx::col::TEXT2, gfx::col::BLACK);
    char d[40];
    gfx::ellipsize(d, sizeof(d), it.detail, 180, gfx::F_SMALL);
    gfx::text5(232 - gfx::textWidth5(d), y, d, c, gfx::col::BLACK);
    (void)idx;
}

}

void setup() {
    Serial.begin(115200);
    setenv("TZ", "UTC0", 1);
    tzset();
    delay(30);

    display::init();
    input.begin();
    cfg::begin();
    if (settings.splash) drawBootLogo();
    else gfx::fillScreen(gfx::col::BLACK);

    buzzer::begin();
    power::begin();

    storage::begin();
    net::begin();
    klog::begin();
    klog::add("boot: %s", IX_NAME);

    gps::begin();

    cc1101.begin();
    nrf24.begin();
    rc522.begin();
    pn532.begin();
    ir.begin();

    power::tick();
    gps::tick();

    int n = selftest::run();
    gfx::text(10, 140, "САМОТЕСТ", gfx::F_SMALL_B, gfx::col::accent, gfx::col::BLACK);
    for (int i = 0; i < n; i++) {
        drawBootItem(i, selftest::at(i), 144 + i * 8);
        if (i % 3 == 0) delay(1);
    }
    klog::add("selftest: %d items, %d failed", n, selftest::failures());

    delay(350);
    app::begin();
    screens::openHome();
    buzzer::start();

    if (selftest::failures() > 0) {
        app::toast("%d module(s) not found", selftest::failures());
    }
}

void loop() {
    app::loopOnce();
    gps::tick();
}
