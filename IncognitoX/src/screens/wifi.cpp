#include <WiFi.h>
#include <esp_wifi.h>
#include <math.h>
#include <time.h>
#include <vector>

#include "app/app.h"
#include "app/log.h"
#include "app/ui.h"
#include "config.h"
#include "hw/buzzer.h"
#include "hw/gfx.h"
#include "hw/gps.h"
#include "hw/input.h"
#include "hw/storage.h"
#include "screens/screens.h"

namespace {

struct ApInfo {
    String          ssid;
    uint8_t         bssid[6] = {0};
    int32_t         rssi = 0;
    uint8_t         ch = 0;
    wifi_auth_mode_t auth = WIFI_AUTH_OPEN;
    bool            hidden = false;
};

std::vector<ApInfo> g_aps;

const char *authName(wifi_auth_mode_t t) {
    switch (t) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA2*";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2E";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
    default: return "?";
    }
}

String macStr(const uint8_t *m) {
    char b[18];
    snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2],
             m[3], m[4], m[5]);
    return String(b);
}

class WifiDetail : public Screen {
public:
    explicit WifiDetail(const ApInfo &ap) : ap_(ap) {}
    const char *title() const override { return "Точка доступа"; }
    const char *hint() const override { return "< Назад    OK сохранить в SD"; }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        int y = UI_CONTENT_Y0 + 8;
        gfx::fillRoundRect(8, y, 224, 54, 8, gfx::col::PANEL);
        gfx::drawIcon(gfx::Icon::Wifi, 18, y + 13, 28, gfx::col::accent);
        char name[32];
        gfx::ellipsize(name, sizeof(name),
                       ap_.hidden ? "<hidden SSID>" : ap_.ssid.c_str(), 236,
                       gfx::F_BODY_B);
        gfx::text(56, y + 28, name, gfx::F_BODY_B, gfx::col::TEXT, gfx::col::PANEL);
        gfx::text(56, y + 46, ap_.hidden ? "скрытая сеть" : "вещание",
                  gfx::F_SMALL, gfx::col::DIM, gfx::col::PANEL);

        y += 64;
        struct Row { const char *k; String v; };
        Row rows[] = {
            {"BSSID", macStr(ap_.bssid)},
            {"Channel", String(ap_.ch)},
            {"Signal", String(ap_.rssi) + " dBm"},
            {"Security", authName(ap_.auth)},
            {"Quality", String(constrain(2 * (ap_.rssi + 100), 0, 100)) + " %"},
        };
        int i = 0;
        for (const auto &r : rows) {
            uint16_t rowBg = (i++ % 2) ? gfx::col::BG : gfx::col::BG2;
            gfx::fillRect(8, y, 224, 22, rowBg);
            gfx::text(16, y + 16, r.k, gfx::F_SMALL, gfx::col::DIM, rowBg);
            gfx::textRight(220, y + 16, r.v.c_str(), gfx::F_SMALL,
                           gfx::col::TEXT, rowBg);
            y += 22;
        }

        y += 8;
        int q = constrain(2 * (ap_.rssi + 100), 0, 100);
        gfx::progressBar(16, y, 208, 10, q, 100, gfx::col::accent, gfx::col::PANEL2);
        gfx::text(16, y + 28, q > 66 ? "отлично" : q > 40 ? "хорошо" : q > 20 ? "слабо" : "очень слабо",
                  gfx::F_SMALL, gfx::col::DIM, bg);
    }

    bool onKey(Key k, KeyEvent e) override {
        if (k == Key::Ok && e == KeyEvent::Press) {
            if (!storage::mounted()) {
                app::toast("no microSD");
                return true;
            }
            storage::ensureDir("/wardrive");
            char line[160];
            snprintf(line, sizeof(line), "%s,%s,%s,%ld,%d,%s",
                     gps::hasFix() ? "fix" : "nofix",
                     ap_.hidden ? "<hidden>" : ap_.ssid.c_str(),
                     macStr(ap_.bssid).c_str(), (long)ap_.rssi, ap_.ch,
                     authName(ap_.auth));
            storage::appendLine("/wardrive/saved_aps.csv", line);
            app::toast("saved to /wardrive");
            klog::add("wifi saved: %s", ap_.ssid.c_str());
            return true;
        }
        return false;
    }

private:
    ApInfo ap_;
};

class WifiScan : public Screen {
public:
    const char *title() const override { return "Сканер Wi-Fi"; }
    const char *hint() const override { return "< back  OK info  hold OK save all"; }

    void enter() override {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        WiFi.setSleep(false);
        startScan();
    }

    void leave() override {
        WiFi.scanDelete();
        WiFi.mode(WIFI_OFF);
    }

    void tick() override {
        if (!scanning_) {
            if (resultsSeen_ && millis() - doneAt_ > 400) {
                resultsSeen_ = false;
                app::invalidate();
            }
            return;
        }
        int16_t r = WiFi.scanComplete();
        if (r >= 0) {
            collect(r);
            WiFi.scanDelete();
            scanning_ = false;
            doneAt_ = millis();
            app::invalidate();
        }
    }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        if (scanning_) {
            gfx::text(12, UI_CONTENT_Y0 + 26, "Поиск Wi-Fi...", gfx::F_BODY_B,
                      gfx::col::accent, bg);
            for (int i = 0; i < 13; i++) {
                int h = 10 + (int)(10 * sinf((millis() / 120.0f) + i * 0.6f));
                gfx::fillRect(16 + i * 22, 150 - h, 14, h, gfx::col::PANEL2);
            }
            gfx::text(12, 190, "скрытые сети включены", gfx::F_SMALL,
                      gfx::col::DIM, bg);
            return;
        }

        if (g_aps.empty()) {
            gfx::text(12, UI_CONTENT_Y0 + 30, "Сети не найдены", gfx::F_BODY_B,
                      gfx::col::DIM, bg);
            gfx::text(12, UI_CONTENT_Y0 + 52, "OK = сканировать снова", gfx::F_SMALL,
                      gfx::col::DIM, bg);
            return;
        }

        const int rowH = 30;
        const int visible = 6;
        if (cursor_ < scroll_) scroll_ = cursor_;
        if (cursor_ >= scroll_ + visible) scroll_ = cursor_ - visible + 1;

        for (int i = 0; i < visible; i++) {
            int idx = scroll_ + i;
            if (idx >= (int)g_aps.size()) break;
            const ApInfo &ap = g_aps[idx];
            int y = UI_CONTENT_Y0 + 2 + i * rowH;
            bool sel = idx == cursor_;
            uint16_t rowBg = sel ? gfx::col::PANEL2 : bg;
            if (sel) gfx::fillRoundRect(4, y, 232, rowH - 2, 5, gfx::col::PANEL2);
            if (sel) gfx::fillRect(4, y + 4, 3, rowH - 10, gfx::col::accent);

            char name[30];
            gfx::ellipsize(name, sizeof(name),
                           ap.hidden ? "<hidden>" : ap.ssid.c_str(), 136,
                           gfx::F_SMALL);
            gfx::text(14, y + 13, name, gfx::F_SMALL,
                      ap.hidden ? gfx::col::WARN : gfx::col::TEXT, rowBg);

            char chbuf[8];
            snprintf(chbuf, sizeof(chbuf), "ch%d", ap.ch);
            gfx::textRight(184, y + 13, chbuf, gfx::F_SMALL, gfx::col::DIM, rowBg);
            if (strcmp(authName(ap.auth), "OPEN") != 0) {
                gfx::text(188, y + 13, "*", gfx::F_SMALL, gfx::col::WARN, rowBg);
            }

            char right[24];
            snprintf(right, sizeof(right), "%ld", (long)ap.rssi);
            gfx::textRight(232, y + 13, right, gfx::F_SMALL, gfx::col::TEXT2, rowBg);

            int q = constrain(2 * (ap.rssi + 100), 0, 100);
            int bars = (q + 24) / 25;
            for (int b = 0; b < 4; b++) {
                int bh = 3 + b * 3;
                gfx::fillRect(196 + b * 5, y + 27 - bh, 3, bh,
                              b < bars ? gfx::col::accent : gfx::col::PANEL3);
            }

            gfx::hline(8, y + rowH - 3, 224, gfx::col::PANEL);
        }

        int total = (int)g_aps.size();
        if (total > visible) {
            int trackH = visible * rowH;
            int barH = max(12, trackH * visible / total);
            int barY = UI_CONTENT_Y0 + 2 + (trackH - barH) * cursor_ / max(1, total - 1);
            gfx::fillRoundRect(232, UI_CONTENT_Y0 + 2, 3, barH, 1, gfx::col::accent);
        }
    }

    bool onKey(Key k, KeyEvent e) override {
        if (k == Key::Ok && e == KeyEvent::Long) {
            saveAll();
            return true;
        }
        if (scanning_) {
            if (k == Key::Ok && e == KeyEvent::Press) return true;
            return false;
        }
        if (g_aps.empty()) {
            if (k == Key::Ok && e == KeyEvent::Press) {
                startScan();
                return true;
            }
            return false;
        }
        switch (k) {
        case Key::Up:
            if (e == KeyEvent::Press || e == KeyEvent::Repeat) {
                cursor_ = (cursor_ - 1 + (int)g_aps.size()) % (int)g_aps.size();
                return true;
            }
            break;
        case Key::Down:
            if (e == KeyEvent::Press || e == KeyEvent::Repeat) {
                cursor_ = (cursor_ + 1) % (int)g_aps.size();
                return true;
            }
            break;
        case Key::Right:
            if (e == KeyEvent::Press) {
                startScan();
                return true;
            }
            break;
        case Key::Ok:
            if (e == KeyEvent::Press) {
                buzzer::ok();
                app::push(new WifiDetail(g_aps[cursor_]));
                return true;
            }
            break;
        default:
            break;
        }
        return false;
    }

private:
    bool     scanning_ = false;
    bool     resultsSeen_ = false;
    uint32_t doneAt_ = 0;
    int      cursor_ = 0;
    int      scroll_ = 0;

    void startScan() {
        g_aps.clear();
        cursor_ = scroll_ = 0;
        int16_t r = WiFi.scanNetworks(true, true);
        scanning_ = (r == WIFI_SCAN_RUNNING || r == WIFI_SCAN_FAILED);
        if (r >= 0) { collect(r); scanning_ = false; }
        app::invalidate();
    }

    void collect(int n) {
        std::vector<ApInfo> tmp;
        tmp.reserve(n);
        for (int i = 0; i < n && (int)tmp.size() < MAX_WIFI_APS; i++) {
            ApInfo ap;
            ap.ssid = WiFi.SSID(i);
            uint8_t b[6];
            memcpy(b, WiFi.BSSID(i), 6);
            memcpy(ap.bssid, b, 6);
            ap.rssi = WiFi.RSSI(i);
            ap.ch = (uint8_t)WiFi.channel(i);
            ap.auth = (wifi_auth_mode_t)WiFi.encryptionType(i);
            ap.hidden = (ap.ssid.length() == 0);
            if (ap.hidden) ap.ssid = "<hidden>";
            tmp.push_back(ap);
        }

        for (size_t i = 1; i < tmp.size(); i++) {
            ApInfo k = tmp[i];
            int j = (int)i - 1;
            while (j >= 0 && tmp[j].rssi < k.rssi) {
                tmp[j + 1] = tmp[j];
                j--;
            }
            tmp[j + 1] = k;
        }
        g_aps = std::move(tmp);
        klog::add("wifi scan: %d AP", (int)g_aps.size());
    }

    void saveAll() {
        if (g_aps.empty()) return;
        if (!storage::mounted()) { app::toast("no microSD"); return; }
        storage::ensureDir("/wardrive");
        for (const auto &ap : g_aps) {
            char line[160];
            snprintf(line, sizeof(line), "%ld,%s,%s,%ld,%d,%s",
                     (long)time(nullptr), ap.ssid.c_str(),
                     macStr(ap.bssid).c_str(), (long)ap.rssi, ap.ch,
                     authName(ap.auth));
            storage::appendLine("/wardrive/ap_list.csv", line);
        }
        app::toast("%d AP saved", (int)g_aps.size());
    }
};

volatile uint32_t g_chan[14];
volatile uint32_t g_mgmt, g_ctrl, g_data, g_beacon, g_probe, g_deauth;

void IRAM_ATTR bump(volatile uint32_t &v) { v = v + 1; }

void zeroCounters() {
    for (int i = 0; i < 14; i++) g_chan[i] = 0;
    g_mgmt = 0;
    g_ctrl = 0;
    g_data = 0;
    g_beacon = 0;
    g_probe = 0;
    g_deauth = 0;
}

void IRAM_ATTR promiscCb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (!buf) return;
    wifi_promiscuous_pkt_t *p = (wifi_promiscuous_pkt_t *)buf;
    uint8_t ch = p->rx_ctrl.channel;
    if (ch < 14) bump(g_chan[ch]);
    switch (type) {
    case WIFI_PKT_MGMT: {
        bump(g_mgmt);
        if (p->rx_ctrl.sig_len >= 1) {
            uint8_t sub = p->payload[0];
            if (sub == 0x80) bump(g_beacon);
            else if (sub == 0x40) bump(g_probe);
            else if (sub == 0xC0 || sub == 0xA0) bump(g_deauth);
        }
        break;
    }
    case WIFI_PKT_CTRL: bump(g_ctrl); break;
    case WIFI_PKT_DATA: bump(g_data); break;
    default: break;
    }
}

class WifiMonitor : public Screen {
public:
    const char *title() const override { return "Воздушный монитор"; }
    const char *hint() const override { return "< back   passive listen"; }

    void enter() override {
        zeroCounters();
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        WiFi.setSleep(false);
        esp_wifi_set_promiscuous_rx_cb(promiscCb);
        esp_wifi_set_promiscuous(true);
        esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
        ch_ = 1;
        lastHop_ = millis();
        klog::add("wifi monitor on");
    }

    void leave() override {
        esp_wifi_set_promiscuous(false);
        esp_wifi_set_promiscuous_rx_cb(nullptr);
        WiFi.mode(WIFI_OFF);
    }

    void tick() override {
        if (millis() - lastHop_ > 250) {
            lastHop_ = millis();
            ch_ = (ch_ % 13) + 1;
            esp_wifi_set_channel(ch_, WIFI_SECOND_CHAN_NONE);
        }
        if (millis() - lastDraw_ > 400) {
            lastDraw_ = millis();
            drawGraph();
        }
    }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        gfx::text(10, UI_CONTENT_Y0 + 18, "Активность 2.4 GHz", gfx::F_SMALL,
                  gfx::col::DIM, bg);
        drawGraph();
    }

    bool onKey(Key k, KeyEvent e) override {
        if (k == Key::Ok && e == KeyEvent::Press) {
            zeroCounters();
            app::invalidate();
            return true;
        }
        return false;
    }

private:
    uint8_t  ch_ = 1;
    uint32_t lastHop_ = 0;
    uint32_t lastDraw_ = 0;
    uint32_t peak_[14] = {0};
    uint32_t maxSeen_ = 1;

    void drawGraph() {
        const uint16_t bg = gfx::col::BG;
        const int baseY = 168;
        const int h = 96;
        gfx::fillRect(0, UI_CONTENT_Y0 + 24, 240, 110, bg);

        uint32_t maxv = 1;
        for (int c = 1; c <= 13; c++) {
            uint32_t v = g_chan[c];
            if (v > maxv) maxv = v;
            if (v > peak_[c]) peak_[c] = v;
        }
        if (maxv > maxSeen_) maxSeen_ = maxv;

        for (int c = 1; c <= 13; c++) {
            int bw = 18;
            int x = 8 + (c - 1) * 23;
            int vh = (int)((uint64_t)h * g_chan[c] / maxSeen_);
            int ph = (int)((uint64_t)h * peak_[c] / maxSeen_);
            gfx::fillRect(x, baseY - h, bw, h, gfx::col::BG2);
            if (vh > 0)
                gfx::fillRect(x, baseY - vh, bw, vh,
                              c == ch_ ? gfx::col::accent : gfx::col::accentDim());
            gfx::hline(x, baseY - ph, bw, gfx::col::WARN);
            char b[4];
            snprintf(b, sizeof(b), "%d", c);
            gfx::text(x + (c >= 10 ? 2 : 5), baseY + 13, b, gfx::F_SMALL,
                      c == ch_ ? gfx::col::accent : gfx::col::DIM, bg);
        }

        char buf[80];
        snprintf(buf, sizeof(buf), "beacon %lu   probe %lu   deauth %lu",
                 (unsigned long)g_beacon, (unsigned long)g_probe,
                 (unsigned long)g_deauth);
        gfx::text5(8, 188, buf, gfx::col::TEXT2, bg);
        snprintf(buf, sizeof(buf), "mgmt %lu   ctrl %lu   data %lu",
                 (unsigned long)g_mgmt, (unsigned long)g_ctrl,
                 (unsigned long)g_data);
        gfx::text5(8, 200, buf, gfx::col::TEXT2, bg);
        snprintf(buf, sizeof(buf), "listening on ch %d", ch_);
        gfx::text(8, 214, buf, gfx::F_SMALL, gfx::col::DIM, bg);
    }
};

class Wardrive : public Screen {
public:
    const char *title() const override { return "Wardriving"; }
    const char *hint() const override { return "< Назад   OK пауза"; }

    void enter() override {
        if (!storage::mounted()) {
            app::message("Нет microSD", "Вставьте карту для записи сетей с GPS-координатами.");
            return;
        }
        storage::ensureDir("/wardrive");
        time_t now = time(nullptr);
        struct tm tmv;
        gmtime_r(&now, &tmv);
        snprintf(path_, sizeof(path_), "/wardrive/wd_%04d%02d%02d.csv",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
        if (!storage::exists(path_))
            storage::writeText(path_, "utc,ssid,bssid,rssi,ch,enc,lat,lon,sats,fix\r\n");
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        startScan();
    }

    void leave() override {
        WiFi.scanDelete();
        WiFi.mode(WIFI_OFF);
    }

    void tick() override {
        if (paused_) return;
        if (scanning_) {
            int16_t r = WiFi.scanComplete();
            if (r >= 0) {
                logResults(r);
                WiFi.scanDelete();
                scanning_ = false;
                nextScan_ = millis() + 2500;
            }
            return;
        }
        if ((int32_t)(millis() - nextScan_) >= 0) startScan();
        if (millis() - lastDraw_ > 500) {
            lastDraw_ = millis();
            app::invalidate();
        }
    }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        int y = UI_CONTENT_Y0 + 10;

        gfx::fillRoundRect(8, y, 224, 70, 8, gfx::col::PANEL);
        char big[16];
        snprintf(big, sizeof(big), "%lu", (unsigned long)logged_);
        gfx::text(20, y + 46, big, gfx::F_BIG_B, gfx::col::accent, gfx::col::PANEL);
        gfx::text(20 + gfx::textWidth(big, gfx::F_BIG_B) + 10, y + 30, "сетей",
                  gfx::F_SMALL, gfx::col::TEXT2, gfx::col::PANEL);
        gfx::text(20 + gfx::textWidth(big, gfx::F_BIG_B) + 10, y + 48,
                  "записано в SD", gfx::F_SMALL, gfx::col::DIM, gfx::col::PANEL);
        gfx::drawIcon(gfx::Icon::Gps, 212, y + 20, 32,
                      gps::hasFix() ? gfx::col::OK : gfx::col::DIM2);

        y += 80;
        const GpsFix &f = gps::fix();
        char line[96];
        if (gps::hasFix())
            snprintf(line, sizeof(line), "%.6f, %.6f  alt %.0f m", f.lat, f.lon,
                     f.alt);
        else
            snprintf(line, sizeof(line), "no GPS fix - logging without coordinates");
        gfx::text(10, y + 14, line, gfx::F_SMALL,
                  gps::hasFix() ? gfx::col::TEXT2 : gfx::col::WARN, bg);

        y += 26;
        snprintf(line, sizeof(line), "sats %d   hdop %.1f   %s", f.sats, f.hdop,
                 paused_ ? "пауза" : scanning_ ? "сканирование..." : "ожидание");
        gfx::text(10, y + 14, line, gfx::F_SMALL, gfx::col::DIM, bg);

        y += 26;
        gfx::text5(10, y, path_, gfx::col::TEXT2, bg);
        y += 14;
        snprintf(line, sizeof(line), "last: %s", lastSsid_[0] ? lastSsid_ : "-");
        gfx::text(10, y + 12, line, gfx::F_SMALL, gfx::col::DIM, bg);
    }

    bool onKey(Key k, KeyEvent e) override {
        if (k == Key::Ok && e == KeyEvent::Press) {
            paused_ = !paused_;
            app::toast(paused_ ? "пауза" : "возобновлено");
            return true;
        }
        return false;
    }

private:
    char     path_[48] = {0};
    bool     scanning_ = false;
    bool     paused_ = false;
    uint32_t logged_ = 0;
    uint32_t nextScan_ = 0;
    uint32_t lastDraw_ = 0;
    char     lastSsid_[24] = {0};

    void startScan() {
        int16_t r = WiFi.scanNetworks(true, true);
        scanning_ = (r == WIFI_SCAN_RUNNING || r == WIFI_SCAN_FAILED);
        if (r >= 0) { logResults(r); scanning_ = false; nextScan_ = millis() + 2500; }
    }

    void logResults(int n) {
        const GpsFix &f = gps::fix();
        char ts[24];
        time_t now = time(nullptr);
        struct tm tmv;
        gmtime_r(&now, &tmv);
        snprintf(ts, sizeof(ts), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour,
                 tmv.tm_min, tmv.tm_sec);
        for (int i = 0; i < n && i < MAX_WIFI_APS; i++) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) ssid = "<hidden>";
            uint8_t *b = WiFi.BSSID(i);
            char line[200];
            snprintf(line, sizeof(line), "%s,%s,%02X:%02X:%02X:%02X:%02X:%02X,%ld,%d,%s,%.6f,%.6f,%d,%d",
                     ts, ssid.c_str(), b[0], b[1], b[2], b[3], b[4], b[5],
                     (long)WiFi.RSSI(i), WiFi.channel(i),
                     authName((wifi_auth_mode_t)WiFi.encryptionType(i)),
                     f.valid ? f.lat : 0.0, f.valid ? f.lon : 0.0, f.sats,
                     f.valid ? 1 : 0);
            storage::appendLine(path_, line);
            logged_++;
            strncpy(lastSsid_, ssid.c_str(), sizeof(lastSsid_) - 1);
        }
        if (n > 0) klog::add("wardrive: +%d (total %lu)", n, (unsigned long)logged_);
    }
};

}

namespace screens {

void openWifi() {
    std::vector<MenuItem> items;
    items.push_back({"Сканировать сети", gfx::Icon::Search, "", true,
                     [] { app::push(new WifiScan()); }});
    items.push_back({"Воздушный монитор", gfx::Icon::Radar, "", true,
                     [] { app::push(new WifiMonitor()); }});
    items.push_back({"Wardriving", gfx::Icon::Map, "", true,
                     [] { app::push(new Wardrive()); }});
    items.push_back({"Перехват WPA рукопожатий", gfx::Icon::Lock, "", false,
                     [] { app::push(new LockedScreen(
                              "Перехват WPA рукопожатий",
                              "Перехват WPA-handshake/PMKID - это предвестник взлома пароля чужой сети, поэтому этот фреймворк не реализует эту функцию.",
                              "Сканируйте сети, наблюдайте за активностью каналов пассивно и логируйте свои AP в SD с GPS-координатами.")); }});
    items.push_back({"Тест deauth", gfx::Icon::Lock, "", false,
                     [] { app::push(new LockedScreen(
                              "Деаутентификация",
                              "Отправка deauth-фреймов отключает чужие устройства и незаконна в большинстве стран. Не реализовано.",
                              "Используйте «Воздушный монитор» для пассивного просмотра счётчиков deauth - это полезно для диагностики ваших собственных Wi-Fi проблем.")); }});
    items.push_back({"Рассылка маяков", gfx::Icon::Lock, "", false,
                     [] { app::push(new LockedScreen(
                              "Beacon spam",
                              "Флудинг поддельных SSID глуши радио и мешает соседям. Не реализовано.",
                              "Wardriving и анализ сканирования дают всю видимость без помех для других.")); }});
    items.push_back({"Ловушка", gfx::Icon::Lock, "", false,
                     [] { app::push(new LockedScreen(
                              "Evil portal",
                              "Фейковая страница входа крадёт учётные данные - это фишинг. Не реализовано.",
                              "Веб-сервер самой системы показывает честную информационную страницу на вашем устройстве, если нужно проверить механику ловушки.")); }});

    app::push(new ListScreen("Wi-Fi", std::move(items)));
}

}
