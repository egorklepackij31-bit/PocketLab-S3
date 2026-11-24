#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_system.h>
#include <time.h>
#include <vector>

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
#include "lib/nrf24.h"
#include "lib/pn532.h"
#include "lib/rc522.h"
#include "pins.h"
#include "screens/screens.h"

namespace {

class DeviceInfo : public Screen {
public:
    const char *title() const override { return "Информация об устройстве"; }
    const char *hint() const override { return "< back"; }

    void enter() override {
        char b[80];
        String t;
        snprintf(b, sizeof(b), "IncognitoX  v" IX_VERSION_STR "\n");
        t += b;
        snprintf(b, sizeof(b), "chip    %s rev %d\n", ESP.getChipModel(),
                 ESP.getChipRevision());
        t += b;
        snprintf(b, sizeof(b), "cores   %d @ %lu MHz\n", ESP.getChipCores(),
                 (unsigned long)getCpuFrequencyMhz());
        t += b;
        snprintf(b, sizeof(b), "flash   %lu MB  (%s)\n",
                 (unsigned long)(ESP.getFlashChipSize() / (1024 * 1024)),
                 ESP.getFlashChipMode() == FM_QIO ? "QIO" : "other");
        t += b;
        snprintf(b, sizeof(b), "heap    %lu KB free, %lu KB min\n",
                 (unsigned long)(ESP.getFreeHeap() / 1024),
                 (unsigned long)(ESP.getMinFreeHeap() / 1024));
        t += b;
        snprintf(b, sizeof(b), "psram   %s\n",
#if IX_USE_PSRAM
                 "enabled (buttons remapped)"
#else
                 "disabled (pins 33-37 free)"
#endif
        );
        t += b;
        snprintf(b, sizeof(b), "mac     %012llX\n", (unsigned long long)ESP.getEfuseMac());
        t += b;
        snprintf(b, sizeof(b), "sd      %s, %lu / %lu MB\n",
                 storage::mounted() ? storage::cardType().c_str() : "none",
                 (unsigned long)(storage::usedBytes() / (1024ULL * 1024ULL)),
                 (unsigned long)(storage::totalBytes() / (1024ULL * 1024ULL)));
        t += b;
        snprintf(b, sizeof(b), "battery %.2f V, %d%%\n", power::voltage(),
                 power::percent());
        t += b;
        snprintf(b, sizeof(b), "temp    %.1f C\n", power::chipTempC());
        t += b;
        snprintf(b, sizeof(b), "uptime  %lu min\n", (unsigned long)(millis() / 60000));
        t += b;
        snprintf(b, sizeof(b), "screens %d deep, log %s\n", app::depth(),
                 klog::toSd() ? "on SD" : "RAM only");
        t += b;
        snprintf(b, sizeof(b), "build   %s %s\n", __DATE__, __TIME__);
        t += b;
        text_ = t;
    }

    void draw() override {
        const char *p = text_.c_str();
        int y = UI_CONTENT_Y0 + 3;
        char buf[80];
        while (*p && y < UI_CONTENT_Y1 - 6) {
            const char *nl = strchr(p, '\n');
            size_t n = nl ? (size_t)(nl - p) : strlen(p);
            if (n >= sizeof(buf)) n = sizeof(buf) - 1;
            memcpy(buf, p, n);
            buf[n] = 0;
            gfx::text5(6, y, buf, gfx::col::TEXT2, gfx::col::BG);
            y += 11;
            if (!nl) break;
            p = nl + 1;
        }
    }

    bool onKey(Key, KeyEvent) override { return false; }

private:
    String text_;
};

class SelfTestScreen : public Screen {
public:
    const char *title() const override { return "Самотест"; }
    const char *hint() const override { return "< Назад   OK повторить"; }

    void enter() override {
        selftest::run();
        app::invalidate();
    }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        int n = selftest::count();
        int y = UI_CONTENT_Y0 + 2;
        for (int i = 0; i < n; i++) {
            const auto &it = selftest::at(i);
            uint16_t c = it.status == selftest::ST_OK      ? gfx::col::OK
                         : it.status == selftest::ST_WARN  ? gfx::col::WARN
                                                           : gfx::col::ERR;
            gfx::drawIcon(it.status == selftest::ST_OK ? gfx::Icon::Check
                                                       : gfx::Icon::Warning,
                          8, y, 10, c);
            gfx::text(24, y + 10, it.name, gfx::F_SMALL, gfx::col::TEXT, bg);
            char d[40];
            gfx::ellipsize(d, sizeof(d), it.detail, 180, gfx::F_SMALL);
            gfx::textRight(220, y + 10, d, gfx::F_SMALL, c, bg);
            y += 15;
        }
        char b[48];
        snprintf(b, sizeof(b), "%d checks, %d failed", n, selftest::failures());
        gfx::text(8, UI_CONTENT_Y1 - 4, b, gfx::F_SMALL,
                  selftest::failures() ? gfx::col::ERR : gfx::col::DIM, bg);
    }

    bool onKey(Key k, KeyEvent e) override {
        if (k == Key::Ok && e == KeyEvent::Press) {
            enter();
            buzzer::ok();
            return true;
        }
        return false;
    }
};

class I2cScan : public Screen {
public:
    const char *title() const override { return "Сканирование I2C"; }
    const char *hint() const override { return "< Назад   OK повторить"; }

    void enter() override { scan(); }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        gfx::text(10, UI_CONTENT_Y0 + 16, "SDA=40  SCL=41  100 kHz", gfx::F_SMALL,
                  gfx::col::DIM, bg);
        if (found_.empty()) {
            gfx::text(10, UI_CONTENT_Y0 + 48, "Устройств нет", gfx::F_BODY_B,
                      gfx::col::WARN, bg);
            drawWrapped("Проверьте питание PN532 и переключатель режима I2C.",
                        10, UI_CONTENT_Y0 + 59, 216, 16, gfx::col::DIM, bg, gfx::F_SMALL);
            return;
        }
        char b[40];
        snprintf(b, sizeof(b), "%d device(s):", (int)found_.size());
        gfx::text(10, UI_CONTENT_Y0 + 38, b, gfx::F_SMALL, gfx::col::TEXT2, bg);

        int y = UI_CONTENT_Y0 + 48;
        for (size_t i = 0; i < found_.size() && y < UI_CONTENT_Y1 - 10; i++) {
            const char *name = found_[i] == PN532_I2C_ADDR ? "PN532 NFC" : "unknown";
            snprintf(b, sizeof(b), "0x%02X   %s", found_[i], name);
            gfx::fillRoundRect(8, y, 224, 18, 4,
                               i & 1 ? gfx::col::BG2 : gfx::col::BG);
            gfx::text(16, y + 13, b, gfx::F_SMALL,
                      found_[i] == PN532_I2C_ADDR ? gfx::col::OK : gfx::col::TEXT2,
                      i & 1 ? gfx::col::BG2 : gfx::col::BG);
            y += 20;
        }
        gfx::text(10, UI_CONTENT_Y1 - 6,
                  pn532.present() ? "PN532 initialized" : "PN532 not initialized",
                  gfx::F_SMALL,
                  pn532.present() ? gfx::col::DIM : gfx::col::WARN, bg);
    }

    bool onKey(Key k, KeyEvent e) override {
        if (k == Key::Ok && e == KeyEvent::Press) {
            scan();
            return true;
        }
        return false;
    }

private:
    std::vector<uint8_t> found_;

    void scan() {
        found_.clear();
        for (uint8_t a = 1; a < 127; a++) {
            Wire.beginTransmission(a);
            if (Wire.endTransmission() == 0) found_.push_back(a);
        }
        app::invalidate();
    }
};

class SpiDevices : public Screen {
public:
    const char *title() const override { return "SPI-устройства"; }
    const char *hint() const override { return "< back   OK refresh"; }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        struct Row { const char *n; bool ok; char d[40]; };
        char b[40];
        Row rows[4];

        rows[0].n = "ST7789 TFT";
        rows[0].ok = true;
        snprintf(rows[0].d, sizeof(rows[0].d), "%dx%d CS=%d", display::width(),
                 display::height(), PIN_TFT_CS);

        rows[1].n = "CC1101";
        rows[1].ok = cc1101.present();
        if (rows[1].ok)
            snprintf(rows[1].d, sizeof(rows[1].d), "ver %02X CS=%d", cc1101.version(),
                     PIN_CC_CS);
        else
            snprintf(rows[1].d, sizeof(rows[1].d), "absent CS=%d", PIN_CC_CS);

        rows[2].n = "NRF24L01";
        rows[2].ok = nrf24.present();
        snprintf(rows[2].d, sizeof(rows[2].d), "%s CS=%d CE=%d",
                 rows[2].ok ? "в сети" : "отсутствует", PIN_NRF_CS, PIN_NRF_CE);

        rows[3].n = "RC522";
        rows[3].ok = rc522.present();
        if (rows[3].ok)
            snprintf(rows[3].d, sizeof(rows[3].d), "ver %02X CS=%d", rc522.version(),
                     PIN_RC522_CS);
        else
            snprintf(rows[3].d, sizeof(rows[3].d), "absent CS=%d", PIN_RC522_CS);

        int y = UI_CONTENT_Y0 + 8;
        for (auto &r : rows) {
            gfx::drawIcon(r.ok ? gfx::Icon::Check : gfx::Icon::Warning, 10, y, 14,
                          r.ok ? gfx::col::OK : gfx::col::ERR);
            gfx::text(30, y + 12, r.n, gfx::F_SMALL, gfx::col::TEXT, bg);
            gfx::textRight(220, y + 12, r.d, gfx::F_SMALL,
                           r.ok ? gfx::col::TEXT2 : gfx::col::WARN, bg);
            y += 28;
        }

        y += 8;
        gfx::hline(10, y, 180, gfx::col::LINE);
        y += 10;
        snprintf(b, sizeof(b), "SCLK=%d MOSI=%d MISO=%d", PIN_TFT_SCLK, PIN_TFT_MOSI,
                 PIN_TFT_MISO);
        gfx::text5(10, y, b, gfx::col::DIM, bg);
        gfx::text5(10, y + 14, "every device has its own CS, all via SPI transactions",
                   gfx::col::DIM, bg);
        gfx::text5(10, y + 28, "TFT up to 40 MHz, modules 6-8 MHz", gfx::col::DIM, bg);
    }

    bool onKey(Key k, KeyEvent e) override {
        (void)k;
        (void)e;
        return false;
    }
};

class UartBridge : public Screen {
public:
    const char *title() const override { return "UART-мост"; }
    const char *hint() const override { return "< back   bridge is live"; }

    void enter() override {
        gps::setPaused(true);
        Serial2.end();
        Serial2.begin(115200, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
        rx_ = tx_ = 0;
        lastDraw_ = 0;
        app::toast("отключите модуль GPS!");
        klog::add("uart bridge on 17/47");
    }

    void leave() override {
        Serial2.end();
        gps::begin();
        gps::setPaused(false);
        klog::add("uart bridge off");
    }

    void tick() override {
        int budget = 128;
        while (budget-- > 0 && Serial.available()) {
            Serial2.write((uint8_t)Serial.read());
            tx_++;
        }
        budget = 128;
        while (budget-- > 0 && Serial2.available()) {
            Serial.write((uint8_t)Serial2.read());
            rx_++;
        }
        if (millis() - lastDraw_ > 300) {
            lastDraw_ = millis();
            app::invalidate();
        }
    }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        int y = UI_CONTENT_Y0 + 8;
        gfx::text(10, y + 16, "USB - UART мост", gfx::F_BODY_B, gfx::col::accent,
                  bg);
        gfx::text(10, y + 36, "115200 8N1, RX 17 / TX 47", gfx::F_SMALL,
                  gfx::col::DIM, bg);

        y += 56;
        char b[48];
        snprintf(b, sizeof(b), "USB -> UART   %lu bytes", (unsigned long)tx_);
        gfx::text(10, y + 16, b, gfx::F_SMALL, gfx::col::TEXT2, bg);
        snprintf(b, sizeof(b), "UART -> USB   %lu bytes", (unsigned long)rx_);
        gfx::text(10, y + 36, b, gfx::F_SMALL, gfx::col::TEXT2, bg);

        y += 62;
        gfx::fillRoundRect(8, y, 224, 40, 6, gfx::col::PANEL);
        gfx::text(16, y + 16, "GPS на паузе", gfx::F_SMALL,
                  gfx::col::WARN, gfx::col::PANEL);
        gfx::text(16, y + 32, "Отключите GPS", gfx::F_SMALL,
                  gfx::col::DIM, gfx::col::PANEL);
    }

    bool onKey(Key, KeyEvent) override { return false; }

private:
    uint32_t rx_ = 0, tx_ = 0, lastDraw_ = 0;
};

WebServer *s_web = nullptr;
uint32_t   s_hits = 0;
String     s_page;

void webRoot() {
    if (!s_web) return;
    s_hits++;
    s_web->send(200, "text/html; charset=utf-8", s_page);
}

class WebInfo : public Screen {
public:
    const char *title() const override { return "Веб-страница"; }
    const char *hint() const override { return "< back   OK stop"; }

    void enter() override {
        WiFi.persistent(false);
        WiFi.mode(WIFI_AP);
        WiFi.softAP("IncognitoX", "incognitox123");
        delay(120);
        buildPage();
        if (!s_web) s_web = new WebServer(80);
        s_web->on("/", webRoot);
        s_web->onNotFound(webRoot);
        s_web->begin();
        s_hits = 0;
        startFailed_ = (WiFi.softAPIP() == IPAddress(0, 0, 0, 0));
        klog::add("web: http://%s/", WiFi.softAPIP().toString().c_str());
    }

    void leave() override {
        if (s_web) {
            s_web->stop();
            delete s_web;
            s_web = nullptr;
        }
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
    }

    void tick() override {
        if (s_web) s_web->handleClient();
        if (millis() - lastDraw_ > 500) {
            lastDraw_ = millis();
            app::invalidate();
        }
    }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        int y = UI_CONTENT_Y0 + 8;
        if (startFailed_) {
            gfx::text(12, y + 20, "Ошибка запуска AP", gfx::F_BODY_B, gfx::col::ERR, bg);
            return;
        }
        IPAddress ip = WiFi.softAPIP();
        char b[48];
        snprintf(b, sizeof(b), "http://%d.%d.%d.%d/", ip[0], ip[1], ip[2], ip[3]);
        gfx::text(12, y + 24, b, gfx::F_BIG_B, gfx::col::accent, bg);
        gfx::text(12, y + 46, "SSID: IncognitoX", gfx::F_SMALL,
                  gfx::col::TEXT2, bg);
        gfx::text(12, y + 62, "пароль: incognitox123", gfx::F_SMALL,
                  gfx::col::TEXT2, bg);

        y += 82;
        y = drawWrapped("Подключитесь с телефона или ноутбука и откройте "
                        "указанный адрес в браузере.",
                        12, y, 216, 16, gfx::col::DIM, bg, gfx::F_SMALL) + 6;
        snprintf(b, sizeof(b), "Запросов: %lu", (unsigned long)s_hits);
        gfx::text(12, y + 14, b, gfx::F_SMALL, gfx::col::TEXT2, bg);
        y += 32;
        drawWrapped("Страница только для чтения: это данные об устройстве.", 12,
                    y, 216, 16, gfx::col::WARN, bg, gfx::F_SMALL);
    }

    bool onKey(Key k, KeyEvent e) override {
        if (k == Key::Ok && e == KeyEvent::Press) {
            app::pop();
            return true;
        }
        return false;
    }

private:
    bool     startFailed_ = false;
    uint32_t lastDraw_ = 0;

    void buildPage() {
        char b[512];
        s_page = F(
            "<!doctype html><meta name=viewport content='width=device-width'>"
            "<title>IncognitoX</title><style>"
            "body{background:#000;color:#eee;font:16px system-ui;margin:0;padding:24px}"
            "h1{color:#ff9000;font-size:28px;margin:0 0 4px}"
            ".s{color:#888;margin-bottom:24px}"
            "div.r{padding:10px 0;border-bottom:1px solid #222}"
            "b{color:#ff9000;display:inline-block;min-width:120px}"
            "</style><h1>IncognitoX</h1><div class=s>ESP32-S3 multi-tool</div>");
        snprintf(b, sizeof(b), "<div class=r><b>Chip</b>%s rev %d, %lu MHz</div>",
                 ESP.getChipModel(), ESP.getChipRevision(),
                 (unsigned long)getCpuFrequencyMhz());
        s_page += b;
        snprintf(b, sizeof(b), "<div class=r><b>Firmware</b>v" IX_VERSION_STR
                               " (%s)</div>", __DATE__);
        s_page += b;
        snprintf(b, sizeof(b), "<div class=r><b>Uptime</b>%lu min</div>",
                 (unsigned long)(millis() / 60000));
        s_page += b;
        snprintf(b, sizeof(b), "<div class=r><b>Free heap</b>%lu KB</div>",
                 (unsigned long)(ESP.getFreeHeap() / 1024));
        s_page += b;
        snprintf(b, sizeof(b), "<div class=r><b>microSD</b>%s, %lu MB</div>",
                 storage::mounted() ? storage::cardType().c_str() : "not mounted",
                 (unsigned long)(storage::totalBytes() / (1024ULL * 1024ULL)));
        s_page += b;
        snprintf(b, sizeof(b), "<div class=r><b>Battery</b>%.2f V, %d%%</div>",
                 power::voltage(), power::percent());
        s_page += b;
        snprintf(b, sizeof(b), "<div class=r><b>GPS</b>%s</div>",
                 gps::hasFix() ? "3D fix" : "no fix");
        s_page += b;
        snprintf(b, sizeof(b), "<div class=r><b>Log lines</b>%d</div>",
                 klog::count());
        s_page += b;
        s_page += F("<p class=s>Read-only page. No control endpoints.</p>");
    }
};

class LogScreen : public Screen {
public:
    const char *title() const override { return "Log"; }
    const char *hint() const override { return "< back   ^v scroll   > end"; }

    void enter() override {
        lines_.clear();
        lines_.push_back(String("session: ") +
                         (klog::toSd() ? klog::sessionPath() : "(RAM only)"));
        lines_.push_back("");
        for (int i = 0; i < klog::count(); i++) lines_.push_back(klog::line(i));
    }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        const int lh = 11;
        const int per = (UI_CONTENT_Y1 - UI_CONTENT_Y0 - 6) / lh;
        if (top_ > (int)lines_.size() - per) top_ = max(0, (int)lines_.size() - per);
        for (int i = 0; i < per; i++) {
            int idx = top_ + i;
            if (idx >= (int)lines_.size()) break;
            gfx::text5(6, UI_CONTENT_Y0 + 4 + i * lh, lines_[idx].c_str(),
                       idx == 0 ? gfx::col::accent : gfx::col::TEXT2, bg);
        }
    }

    bool onKey(Key k, KeyEvent e) override {
        if (e != KeyEvent::Press && e != KeyEvent::Repeat) return false;
        const int lh = 11;
        const int per = (UI_CONTENT_Y1 - UI_CONTENT_Y0 - 6) / lh;
        int maxTop = max(0, (int)lines_.size() - per);
        if (k == Key::Up) { top_ = max(0, top_ - 1); return true; }
        if (k == Key::Down) { top_ = min(maxTop, top_ + 1); return true; }
        if (k == Key::Right) { top_ = maxTop; return true; }
        return false;
    }

private:
    std::vector<String> lines_;
    int top_ = 0;
};

class SettingsScreen : public Screen {
public:
    const char *title() const override { return "Настройки"; }
    const char *hint() const override {
        return "< Назад   ^v выбор   OK изменить";
    }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        const int top = UI_CONTENT_Y0 + 2;
        const int rowH = 21;
        static const char *names[] = {"Яркость",  "Громкость", "Звук",
                                      "Акцент",   "Часовой пояс", "Автосон",
                                      "Запись в SD", "Заставка", "Сброс настроек"};
        for (int i = 0; i < 9; i++) {
            int y = top + i * rowH;
            bool sel = i == cursor_;
            if (sel) {
                gfx::fillRoundRect(4, y, 232, rowH - 2, 5, gfx::col::PANEL2);
                gfx::fillRect(4, y + 3, 3, rowH - 8, gfx::col::accent);
            }
            gfx::text(16, y + 14, names[i], gfx::F_SMALL,
                      sel ? gfx::col::TEXT : gfx::col::TEXT2,
                      sel ? gfx::col::PANEL2 : bg);
            char v[32];
            value(i, v, sizeof(v));
            gfx::textRight(220, y + 14, v, gfx::F_SMALL,
                           i == 8 ? gfx::col::WARN
                                  : sel ? gfx::col::accent : gfx::col::DIM,
                           sel ? gfx::col::PANEL2 : bg);
        }
    }

    bool onKey(Key k, KeyEvent e) override {
        if (k == Key::Up && (e == KeyEvent::Press || e == KeyEvent::Repeat)) {
            cursor_ = (cursor_ + 8) % 9;
            return true;
        }
        if (k == Key::Down && (e == KeyEvent::Press || e == KeyEvent::Repeat)) {
            cursor_ = (cursor_ + 1) % 9;
            return true;
        }
        if (k == Key::Ok && (e == KeyEvent::Press || e == KeyEvent::Long)) {
            int delta = (e == KeyEvent::Long) ? -1 : 1;
            if (cursor_ == 8) {
                if (app::confirm("Сбросить настройки?", "Все параметры вернутся к значениям по умолчанию.")) {
                    cfg::reset();
                    app::toast("настройки сброшены");
                }
                return true;
            }
            change(cursor_, delta);
            return true;
        }
        return false;
    }

private:
    int cursor_ = 0;

    void value(int i, char *out, size_t n) {
        switch (i) {
        case 0: snprintf(out, n, "%u%%", settings.brightness); break;
        case 1: snprintf(out, n, "%u%%", settings.volume); break;
        case 2: snprintf(out, n, "%s", settings.sound ? "Вкл" : "Выкл"); break;
        case 3: snprintf(out, n, "%s", cfg::accentName(settings.accent)); break;
        case 4: snprintf(out, n, "UTC%+d", settings.tz); break;
        case 5:
            if (settings.autosleep == 0) snprintf(out, n, "Выкл");
            else snprintf(out, n, "%u min", settings.autosleep);
            break;
        case 6: snprintf(out, n, "%s", settings.sdlog ? "Вкл" : "Выкл"); break;
        case 7: snprintf(out, n, "%s", settings.splash ? "Вкл" : "Выкл"); break;
        default: snprintf(out, n, "удержание OK"); break;
        }
    }

    void change(int i, int d) {
        switch (i) {
        case 0: {
            int v = (int)settings.brightness + d * 5;
            settings.brightness = (uint8_t)constrain(v, 5, 100);
            break;
        }
        case 1: {
            int v = (int)settings.volume + d * 10;
            settings.volume = (uint8_t)constrain(v, 0, 100);
            break;
        }
        case 2: settings.sound = settings.sound ? 0 : 1; break;
        case 3: {
            int n = cfg::accentCount();
            settings.accent = (uint8_t)((settings.accent + d + n) % n);
            break;
        }
        case 4: {
            int v = (int)settings.tz + d;
            settings.tz = (int8_t)constrain(v, -12, 14);
            break;
        }
        case 5: {
            static const uint8_t opts[] = {0, 5, 10, 15, 30, 60};
            int idx = 0;
            for (int j = 0; j < 6; j++)
                if (opts[j] == settings.autosleep) idx = j;
            idx = (idx + d + 6) % 6;
            settings.autosleep = opts[idx];
            break;
        }
        case 6: settings.sdlog = settings.sdlog ? 0 : 1; break;
        case 7: settings.splash = settings.splash ? 0 : 1; break;
        default: break;
        }
        cfg::save();
        cfg::apply();
        buzzer::beep(2000, 20);
        app::invalidate();
    }
};

}

namespace screens {

Screen *makeSelftest() { return new SelfTestScreen(); }

void openSystem() {
    std::vector<MenuItem> items;
    items.push_back({"Информация об устройстве", gfx::Icon::Info, "", true,
                     [] { app::push(new DeviceInfo()); }});
    items.push_back({"Самотест", gfx::Icon::Check, "", true,
                     [] { app::push(makeSelftest()); }});
    items.push_back({"Сканирование I2C", gfx::Icon::Search, "", true,
                     [] { app::push(new I2cScan()); }});
    items.push_back({"SPI-устройства", gfx::Icon::Chip, "", true,
                     [] { app::push(new SpiDevices()); }});
    items.push_back({"UART-мост", gfx::Icon::Terminal, "", true,
                     [] { app::push(new UartBridge()); }});
    items.push_back({"Веб-страница", gfx::Icon::Web, "", true,
                     [] { app::push(new WebInfo()); }});
    items.push_back({"Лог", gfx::Icon::Log, "", true,
                     [] { app::push(new LogScreen()); }});
    items.push_back({"Перезагрузка", gfx::Icon::Warning, "", true, [] {
                         if (app::confirm("Перезагрузить?", "Устройство перезапустится.")) {
                             app::toast("перезагрузка...");
                             delay(150);
                             ESP.restart();
                         }
                     }});
    app::push(new ListScreen("Система", std::move(items),
                             "< Назад   OK открыть   ^v движение"));
}

void openSettings() { app::push(new SettingsScreen()); }

}
