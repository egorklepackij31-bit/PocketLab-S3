#include <math.h>
#include <vector>

#include "app/app.h"
#include "app/log.h"
#include "app/ui.h"
#include "config.h"
#include "hw/buzzer.h"
#include "hw/gfx.h"
#include "hw/input.h"
#include "hw/storage.h"
#include "lib/cc1101.h"
#include "lib/nrf24.h"
#include "lib/pulse.h"
#include "screens/screens.h"

namespace {

constexpr int   SP_N      = 60;
constexpr float SP_LO     = 433.00f;
constexpr float SP_HI     = 434.90f;
constexpr int   SP_X0     = 8;
constexpr int   SP_PITCH  = 5;
constexpr int   SP_BW     = 4;
constexpr int   SP_BASE   = 172;
constexpr int   SP_TOP    = 58;

class SubGhzSpectrum : public Screen {
public:
    const char *title() const override { return "Спектр 433"; }
    const char *hint() const override {
        return frozen_ ? "< Назад   OK возобновить" : "< Назад   OK заморозить";
    }

    void enter() override {
        if (!cc1101.present()) return;
        cc1101.setOOK();
        cc1101.setFrequency(433.92f);
        sampleAll();
        last_ = millis();
    }

    void leave() override {
        cc1101.setFrequency(433.92f);
        cc1101.setRx();
    }

    void tick() override {
        if (frozen_ || !cc1101.present()) return;
        if (millis() - last_ < 220) return;
        last_ = millis();
        sampleAll();
        app::invalidate();
    }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        if (!cc1101.present()) {
            gfx::text(12, UI_CONTENT_Y0 + 40, "CC1101 не найден", gfx::F_BODY_B,
                      gfx::col::ERR, bg);
            drawWrapped("Проверьте CS=6, GDO0=7, питание 3.3 V", 12,
                        UI_CONTENT_Y0 + 51, 216, 16, gfx::col::DIM, bg, gfx::F_SMALL);
            return;
        }

        char buf[64];
        snprintf(buf, sizeof(buf), "%.0f - %.0f MHz   %s", SP_LO, SP_HI,
                 frozen_ ? "заморозка" : "в эфире");
        gfx::text(10, UI_CONTENT_Y0 + 16, buf, gfx::F_SMALL,
                  frozen_ ? gfx::col::WARN : gfx::col::DIM, bg);

        gfx::hline(SP_X0, SP_BASE, SP_N * SP_PITCH, gfx::col::LINE);
        for (int i = 0; i < SP_N; i++) {
            int x = SP_X0 + i * SP_PITCH;
            int h = (int)((rssi_[i] + 100.0f) * (SP_BASE - SP_TOP) / 75.0f);
            h = constrain(h, 1, SP_BASE - SP_TOP);
            int hp = (int)((peak_[i] + 100.0f) * (SP_BASE - SP_TOP) / 75.0f);
            hp = constrain(hp, 1, SP_BASE - SP_TOP);
            uint16_t c = rssi_[i] > -55.0f ? gfx::col::ORANGE
                         : rssi_[i] > -70.0f ? gfx::col::accentDim()
                                             : gfx::col::PANEL3;
            gfx::fillRect(x, SP_BASE - h, SP_BW, h, c);
            gfx::fillRect(x, SP_BASE - hp - 2, SP_BW, 2, gfx::col::WARN);
        }

        for (int i = 0; i < SP_N; i += 10) {
            float f = SP_LO + (SP_HI - SP_LO) * i / (SP_N - 1);
            char l[8];
            snprintf(l, sizeof(l), "%.0f", f);
            gfx::text(SP_X0 + i * SP_PITCH - 2, SP_BASE + 13, l, gfx::F_SMALL,
                      gfx::col::DIM, bg);
        }

        snprintf(buf, sizeof(buf), "peak %.1f dBm   floor %.1f dBm",
                 maxRssi_, floorRssi_);
        gfx::text(10, 196, buf, gfx::F_SMALL, gfx::col::TEXT2, bg);
        drawWrapped("Устройства OOK/ASK: пульты, сенсоры, сокеты", 10, 198, 220, 16,
                    gfx::col::DIM, bg, gfx::F_SMALL);
    }

    bool onKey(Key k, KeyEvent e) override {
        if (k == Key::Ok && e == KeyEvent::Press) {
            frozen_ = !frozen_;
            app::toast(frozen_ ? "заморожено" : "в эфире");
            return true;
        }
        return false;
    }

private:
    float    rssi_[SP_N] = {0};
    float    peak_[SP_N] = {0};
    float    maxRssi_ = -100.0f, floorRssi_ = -100.0f;
    bool     frozen_ = false;
    uint32_t last_ = 0;

    void sampleAll() {
        float mn = 0.0f, mx = -100.0f;
        for (int i = 0; i < SP_N; i++) {
            float f = SP_LO + (SP_HI - SP_LO) * i / (SP_N - 1);
            cc1101.setFrequency(f);
            cc1101.setRx();
            delayMicroseconds(320);
            float r = cc1101.rssiDbm();
            rssi_[i] = r;
            if (r > peak_[i]) peak_[i] = r;
            if (r > mx) mx = r;
            mn += r;
        }
        maxRssi_ = mx;
        floorRssi_ = mn / SP_N;
        cc1101.setFrequency(433.92f);
        cc1101.setRx();
    }
};

class SubGhzCapture : public Screen {
public:
    const char *title() const override { return "OOK захват"; }
    const char *hint() const override {
        return "< Назад   v новая   OK сохранить";
    }

    void enter() override {
        if (!cc1101.present()) {
            app::message("CC1101", "Receiver not found. Check wiring and 3.3 V supply.");
            return;
        }
        restart();
    }

    void leave() override {
        cc1101.captureStop();
        cc1101.setOOK();
    }

    void tick() override {
        if (!capturing_ || !cc1101.present()) return;
        uint16_t n = cc1101.capturedEdges();
        bool full = n >= PULSE_MAX_EDGES - 2;
        bool done = n > 6 && cc1101.captureTimedOut(90000);
        bool slow = millis() - started_ > 1500;
        if (full || done || slow) finish();
    }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        if (!cc1101.present()) {
            gfx::text(12, UI_CONTENT_Y0 + 40, "CC1101 не найден", gfx::F_BODY_B,
                      gfx::col::ERR, bg);
            return;
        }

        int y = UI_CONTENT_Y0 + 4;
        if (capturing_) {
            gfx::text(12, y + 18, "Слушаем OOK...", gfx::F_BODY_B,
                      gfx::col::accent, bg);
            char b[48];
            snprintf(b, sizeof(b), "edges: %u   %.1f ms", cc1101.capturedEdges(),
                     cc1101.captureSpanUs() / 1000.0f);
            gfx::text(12, y + 40, b, gfx::F_SMALL, gfx::col::TEXT2, bg);
            gfx::text(12, y + 58, "Нажмите пульт 433 MHz",
                      gfx::F_SMALL, gfx::col::DIM, bg);
            drawWave(y + 74);
            return;
        }

        if (!code_.valid) {
            gfx::text(12, y + 18, "Нет OOK-кадра", gfx::F_BODY_B,
                      gfx::col::WARN, bg);
            gfx::text(12, y + 40, "Поднесите пульт ближе", gfx::F_SMALL,
                      gfx::col::TEXT2, bg);
            gfx::text(12, y + 56, "DOWN = армировка", gfx::F_SMALL,
                      gfx::col::DIM, bg);
            drawWave(y + 74);
            return;
        }

        gfx::fillRoundRect(8, y + 2, 224, 46, 8, gfx::col::PANEL);
        gfx::text(18, y + 22, "КОД", gfx::F_SMALL, gfx::col::DIM,
                  gfx::col::PANEL);
        gfx::text(18, y + 44, code_.hex, gfx::F_BIG_B, gfx::col::accent,
                  gfx::col::PANEL);
        char b[32];
        snprintf(b, sizeof(b), "%u bits   %u frame(s)", code_.bits, code_.frames);
        gfx::textRight(220, y + 22, b, gfx::F_SMALL, gfx::col::TEXT2,
                       gfx::col::PANEL);
        snprintf(b, sizeof(b), "short %lu us  long %lu us",
                 (unsigned long)code_.shortUs, (unsigned long)code_.longUs);
        gfx::textRight(220, y + 44, b, gfx::F_SMALL, gfx::col::DIM,
                       gfx::col::PANEL);

        int yy = y + 58;
        gfx::text5(12, yy, code_.chips[0] ? code_.chips : "-", gfx::col::TEXT2,
                   bg);
        yy += 14;
        snprintf(b, sizeof(b), "edges %u   span %.1f ms   %s", edges_,
                 spanMs_, saved_ ? "SAVED" : "not saved");
        gfx::text(12, yy + 10, b, gfx::F_SMALL,
                  saved_ ? gfx::col::OK : gfx::col::DIM, bg);

        drawWave(yy + 16);
    }

    bool onKey(Key k, KeyEvent e) override {
        if (!cc1101.present()) return false;
        if (k == Key::Down && (e == KeyEvent::Press || e == KeyEvent::Repeat)) {
            restart();
            return true;
        }
        if (k == Key::Ok && e == KeyEvent::Press) {
            save();
            return true;
        }
        return false;
    }

private:
    OokCode  code_;
    bool     capturing_ = false;
    bool     saved_ = false;
    uint16_t edges_ = 0;
    float    spanMs_ = 0;
    uint32_t started_ = 0;

    void restart() {
        code_ = OokCode();
        saved_ = false;
        edges_ = 0;
        spanMs_ = 0;
        cc1101.setFrequency(433.92f);
        cc1101.setOOK();
        cc1101.captureStart();
        capturing_ = true;
        started_ = millis();
        app::invalidate();
    }

    void finish() {
        cc1101.captureStop();
        capturing_ = false;
        edges_ = cc1101.capturedEdges();
        spanMs_ = cc1101.captureSpanUs() / 1000.0f;
        if (edges_ >= 8) {
            code_ = ookDecode(cc1101.captured(), edges_ > 1 ? edges_ - 1 : 0);
        }
        if (code_.valid) {
            buzzer::ok();
            klog::add("subghz: %s (%u bits)", code_.hex, code_.bits);
        } else {
            buzzer::err();
        }
        app::invalidate();
    }

    void drawWave(int top) {
        if (edges_ < 4) return;
        const int x0 = 12, x1 = 220;
        const int mid = top + 42;
        gfx::hline(x0, mid, x1 - x0, gfx::col::PANEL2);
        int n = edges_ - 1;
        if (n > 160) n = 160;
        uint32_t maxUs = 1;
        for (int i = 0; i < n; i++)
            if (cc1101.captured()[i] > maxUs) maxUs = cc1101.captured()[i];
        int px = x0;
        bool high = true;
        for (int i = 0; i < n && px < x1; i++) {
            int w = (int)((int64_t)(x1 - x0) * cc1101.captured()[i] / maxUs);
            if (w < 1) w = 1;
            int y = high ? mid - 22 : mid + 22;
            gfx::fillRect(px, high ? y : mid, w, high ? 22 : 22,
                          high ? gfx::col::accent : gfx::col::PANEL3);
            px += w;
            high = !high;
        }
    }

    void save() {
        if (!code_.valid) {
            app::toast("nothing to save");
            return;
        }
        if (!storage::mounted()) {
            app::toast("no microSD");
            return;
        }
        storage::ensureDir(SUBGHZ_DIR);
        char path[48];
        snprintf(path, sizeof(path), SUBGHZ_DIR "/ook_%08lX.txt",
                 (unsigned long)code_.value);
        char buf[640];
        int n = snprintf(buf, sizeof(buf),
                         "# IncognitoX sub-GHz OOK capture\n"
                         "freq_mhz=433.92\nprotocol=OOK\ncode=%s\nbits=%u\n"
                         "frames=%u\nshort_us=%lu\nlong_us=%lu\nchips=%s\n"
                         "edges=%u\nspan_ms=%.2f\nintervals_us=",
                         code_.hex, code_.bits, code_.frames,
                         (unsigned long)code_.shortUs,
                         (unsigned long)code_.longUs, code_.chips, edges_,
                         spanMs_);
        int lim = min((int)sizeof(buf) - 16, edges_ > 1 ? edges_ - 1 : 0);
        for (int i = 0; i < lim && n < (int)sizeof(buf) - 16; i++)
            n += snprintf(buf + n, sizeof(buf) - n, "%lu,",
                          (unsigned long)cc1101.captured()[i]);
        storage::writeText(path, buf);
        saved_ = true;
        app::toast("saved %s", path);
        klog::add("subghz saved: %s", path);
    }
};

class SubGhzFiles : public Screen {
public:
    const char *title() const override { return "Файлы Sub-GHz"; }
    const char *hint() const override { return "< back   OK view   hold OK del"; }

    void enter() override { reload(); }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        if (!storage::mounted()) {
            gfx::text(12, UI_CONTENT_Y0 + 30, "No microSD card", gfx::F_BODY_B,
                      gfx::col::WARN, bg);
            return;
        }
        if (entries_.empty()) {
            gfx::text(12, UI_CONTENT_Y0 + 30, "Сохранений нет", gfx::F_BODY_B,
                      gfx::col::DIM, bg);
            gfx::text(12, UI_CONTENT_Y0 + 54, "OOK захват > OK", gfx::F_SMALL,
                      gfx::col::DIM, bg);
            return;
        }
        const int rowH = 30, visible = 6;
        if (cursor_ < scroll_) scroll_ = cursor_;
        if (cursor_ >= scroll_ + visible) scroll_ = cursor_ - visible + 1;
        for (int i = 0; i < visible; i++) {
            int idx = scroll_ + i;
            if (idx >= (int)entries_.size()) break;
            int y = UI_CONTENT_Y0 + 2 + i * rowH;
            bool sel = idx == cursor_;
            if (sel) {
                gfx::fillRoundRect(4, y, 232, rowH - 2, 5, gfx::col::PANEL2);
                gfx::fillRect(4, y + 4, 3, rowH - 10, gfx::col::accent);
            }
            uint16_t rowBg = sel ? gfx::col::PANEL2 : bg;
            gfx::drawIcon(gfx::Icon::SubGhz, 12, y + 6, 18,
                          sel ? gfx::col::accent : gfx::col::DIM);
            char nm[30];
            gfx::ellipsize(nm, sizeof(nm), entries_[idx].name.c_str(), 180,
                           gfx::F_SMALL);
            gfx::text(38, y + 14, nm, gfx::F_SMALL, gfx::col::TEXT, rowBg);
            char sz[16];
            snprintf(sz, sizeof(sz), "%u B", (unsigned)entries_[idx].size);
            gfx::textRight(220, y + 14, sz, gfx::F_SMALL, gfx::col::DIM, rowBg);
            gfx::text(38, y + 25, entries_[idx].name.c_str(), gfx::F_SMALL,
                      gfx::col::DIM2, rowBg);
        }
    }

    bool onKey(Key k, KeyEvent e) override {
        if (entries_.empty()) return false;
        switch (k) {
        case Key::Up:
            if (e == KeyEvent::Press || e == KeyEvent::Repeat) {
                cursor_ = (cursor_ - 1 + (int)entries_.size()) % (int)entries_.size();
                return true;
            }
            break;
        case Key::Down:
            if (e == KeyEvent::Press || e == KeyEvent::Repeat) {
                cursor_ = (cursor_ + 1) % (int)entries_.size();
                return true;
            }
            break;
        case Key::Ok:
            if (e == KeyEvent::Long) {
                char path[64];
                snprintf(path, sizeof(path), SUBGHZ_DIR "/%s",
                         entries_[cursor_].name.c_str());
                if (app::confirm("Delete file?", path)) {
                    storage::remove(path);
                    reload();
                }
                return true;
            }
            if (e == KeyEvent::Press) {
                char path[64];
                snprintf(path, sizeof(path), SUBGHZ_DIR "/%s",
                         entries_[cursor_].name.c_str());
                String txt = storage::readText(path, 4000);
                TextScreen *ts = new TextScreen("Capture detail");
                app::push(ts);
                ts->setText(txt.c_str());
                return true;
            }
            break;
        default:
            break;
        }
        return false;
    }

private:
    std::vector<storage::Entry> entries_;
    int cursor_ = 0, scroll_ = 0;

    void reload() {
        storage::list(SUBGHZ_DIR, entries_);
        if (cursor_ >= (int)entries_.size()) cursor_ = max(0, (int)entries_.size() - 1);
        app::invalidate();
    }
};

constexpr int NRF_CH = 125;

class NrfScanner : public Screen {
public:
    const char *title() const override { return "Сканер 2.4 GHz"; }
    const char *hint() const override { return "< back   OK reset peaks"; }

    void enter() override {
        memset(hits_, 0, sizeof(hits_));
        memset(peak_, 0, sizeof(peak_));
        nrf24.rx();
    }

    void tick() override {
        if (!nrf24.present()) return;
        if (millis() - last_ < 90) return;
        last_ = millis();
        sweep();
        app::invalidate();
    }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        if (!nrf24.present()) {
            gfx::text(12, UI_CONTENT_Y0 + 40, "NRF24L01 нет", gfx::F_BODY_B,
                      gfx::col::ERR, bg);
            drawWrapped("Проверьте CS=18, CE=46 и 3.3 V, 10 uF", 12,
                        UI_CONTENT_Y0 + 51, 216, 16, gfx::col::DIM, bg, gfx::F_SMALL);
            return;
        }

        char buf[72];
        snprintf(buf, sizeof(buf), "RPD map 2400-2525 MHz   peak ch %d", topCh_);
        gfx::text(10, UI_CONTENT_Y0 + 16, buf, gfx::F_SMALL, gfx::col::DIM, bg);

        const int base = 168, top = 44, x0 = 8;
        gfx::hline(x0, base, NRF_CH * 2, gfx::col::LINE);
        for (int i = 0; i < NRF_CH; i++) {
            int x = x0 + i * 2;
            int h = (int)((uint32_t)hits_[i] * (base - top) / maxSeen_);
            int p = (int)((uint32_t)peak_[i] * (base - top) / maxSeen_);
            if (h > 0) {
                uint16_t c = hits_[i] > maxSeen_ / 2 ? gfx::col::ORANGE
                                                      : gfx::col::accentDim();
                gfx::fillRect(x, base - h, 2, h, c);
            }
            if (peak_[i] > 0) gfx::fillRect(x, base - p - 1, 2, 1, gfx::col::WARN);
        }
        for (int i = 0; i < NRF_CH; i += 25) {
            char l[8];
            snprintf(l, sizeof(l), "%d", 2400 + i);
            gfx::text(x0 + i * 2 - 4, base + 13, l, gfx::F_SMALL, gfx::col::DIM,
                      bg);
        }
        snprintf(buf, sizeof(buf), "ch %d  %d MHz   top: ch %d   scale %u",
                 nrf24.channel(), 2400 + nrf24.channel(), topCh_,
                 (unsigned)maxSeen_);
        gfx::text(10, 196, buf, gfx::F_SMALL, gfx::col::TEXT2, bg);
        drawWrapped("Wi-Fi, BLE, мыши, видео и микроволновки", 10, 198, 220, 16,
                    gfx::col::DIM, bg, gfx::F_SMALL);
    }

    bool onKey(Key k, KeyEvent e) override {
        if (k == Key::Ok && e == KeyEvent::Press) {
            memset(peak_, 0, sizeof(peak_));
            memset(hits_, 0, sizeof(hits_));
            maxSeen_ = 1;
            topCh_ = 0;
            app::toast("сброс пиков");
            return true;
        }
        return false;
    }

private:
    uint16_t hits_[NRF_CH] = {0};
    uint16_t peak_[NRF_CH] = {0};
    uint32_t maxSeen_ = 1;
    uint32_t last_ = 0;
    int      topCh_ = 0;

    void sweep() {

        for (int i = 0; i < NRF_CH; i++) {
            hits_[i] = (uint16_t)(hits_[i] * 7 / 8);
        }
        for (int i = 0; i < NRF_CH; i++) {
            nrf24.setChannel((uint8_t)i);
            if (nrf24.carrierDetected() && hits_[i] < 60000) hits_[i] += 6;
            if (hits_[i] > peak_[i]) peak_[i] = hits_[i];
            if (peak_[i] > maxSeen_) maxSeen_ = peak_[i];
        }

        uint16_t best = 0;
        for (int i = 0; i < NRF_CH; i++) {
            if (hits_[i] > best) {
                best = hits_[i];
                topCh_ = i;
            }
        }
    }
};

}

namespace screens {

void openSubGhz() {
    std::vector<MenuItem> items;
    items.push_back({"Спектрометр", gfx::Icon::Radar, "", true,
                     [] { app::push(new SubGhzSpectrum()); }});
    items.push_back({"OOK захват / декодирование", gfx::Icon::Signal, "", true,
                     [] { app::push(new SubGhzCapture()); }});
    items.push_back({"Сохранённые захваты", gfx::Icon::Folder, "", true,
                     [] { app::push(new SubGhzFiles()); }});
    items.push_back({"Передача / повтор", gfx::Icon::Lock, "", false,
                     [] { app::push(new LockedScreen(
                              "Sub-GHz transmit",
                              "Replaying or brute-forcing 433 MHz codes opens gates, "
                              "garages and barriers that are not yours. Radio "
                              "transmission is therefore not implemented in this "
                              "firmware.",
                              "Use the spectrum and OOK capture tools to identify and "
                              "analyse your own devices, then control them with their "
                              "original remote.")); }});
    items.push_back({"Brute-force remotes", gfx::Icon::Lock, "", false,
                     [] { app::push(new LockedScreen(
                              "Fixed-code brute force",
                              "Sweeping all codes of a fixed-code remote to open "
                              "someone else's barrier is theft. Not implemented.",
                              "Capture a code and compare it with a second press to "
                              "see whether your device uses fixed or rolling codes.")); }});
    app::push(new ListScreen("Sub-GHz 433", std::move(items),
                             "< back   OK open   ^v move"));
}

void openNrf() { app::push(new NrfScanner()); }

}
