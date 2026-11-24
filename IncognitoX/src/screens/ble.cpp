#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <vector>

#include "app/app.h"
#include "app/log.h"
#include "app/ui.h"
#include "config.h"
#include "hw/buzzer.h"
#include "hw/gfx.h"
#include "hw/storage.h"
#include "screens/screens.h"

namespace {

struct BleDev {
    char     addr[18] = {0};
    char     name[26] = {0};
    int8_t   rssi = -127;
    bool     tracker = false;
    bool     connectable = false;
    uint16_t company = 0xFFFF;
    char     adv[65] = {0};
    uint16_t advLen = 0;
    uint16_t seen = 0;
};

std::vector<BleDev> g_devs;
SemaphoreHandle_t   g_mtx = nullptr;
volatile bool       g_running = false;
volatile bool       g_done = false;
uint32_t            g_seconds = 6;
bool                g_bleInit = false;

const char *companyName(uint16_t id) {
    switch (id) {
    case 0x004C: return "Apple";
    case 0x0075: return "Samsung";
    case 0x0006: return "Microsoft";
    case 0x000F: return "Broadcom";
    case 0x00E0: return "Google";
    case 0x0059: return "Nordic";
    case 0x0499: return "Ruuvi";
    case 0x0157: return "Xiaomi";
    case 0x0171: return "Amazon";
    default: return nullptr;
    }
}

bool looksLikeTracker(const String &name, uint16_t company, const String &mfg,
                      const String &svc) {
    if (company == 0x004C) {

        for (size_t i = 0; i < mfg.length(); i++)
            if ((uint8_t)mfg[i] == 0x12) return true;
        if (name.indexOf("AirTag") >= 0) return true;
    }
    if (company == 0x0075) return true;
    if (svc.indexOf("feed") >= 0) return true;
    if (svc.indexOf("fe9f") >= 0) return true;
    if (name.indexOf("Tile") >= 0) return true;
    if (name.indexOf("SmartTag") >= 0) return true;
    if (name.indexOf("Chipolo") >= 0) return true;
    return false;
}

class AdvCb : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice dev) override {
        String addr = dev.getAddress().toString().c_str();
        String name = dev.haveName() ? dev.getName() : String("");
        String mfg = dev.haveManufacturerData() ? dev.getManufacturerData() : String("");
        String svc = dev.haveServiceUUID() ? dev.getServiceUUID().toString() : String("");

        uint16_t company = 0xFFFF;
        if (mfg.length() >= 2) {
            company = (uint8_t)mfg[0] | ((uint16_t)(uint8_t)mfg[1] << 8);
        }
        bool tracker = looksLikeTracker(name, company, mfg, svc);

        char hex[65] = {0};
        uint8_t *pl = dev.getPayload();
        size_t plLen = dev.getPayloadLength();
        if (pl && plLen) {
            size_t n = min(plLen, (size_t)32);
            for (size_t i = 0; i < n; i++)
                snprintf(hex + i * 2, 3, "%02X", pl[i]);
            hex[n * 2] = 0;
        }

        if (g_mtx) xSemaphoreTake(g_mtx, portMAX_DELAY);
        bool found = false;
        for (auto &d : g_devs) {
            if (strcmp(d.addr, addr.c_str()) == 0) {
                d.rssi = (int8_t)dev.getRSSI();
                d.seen++;
                if (name.length() && !d.name[0])
                    strncpy(d.name, name.c_str(), sizeof(d.name) - 1);
                d.tracker = d.tracker || tracker;
                found = true;
                break;
            }
        }
        if (!found && g_devs.size() < MAX_BLE_DEVS) {
            BleDev d;
            strncpy(d.addr, addr.c_str(), sizeof(d.addr) - 1);
            if (name.length()) strncpy(d.name, name.c_str(), sizeof(d.name) - 1);
            d.rssi = (int8_t)dev.getRSSI();
            d.tracker = tracker;
            d.connectable = dev.isConnectable();
            d.company = company;
            strncpy(d.adv, hex, sizeof(d.adv) - 1);
            d.advLen = (uint16_t)strlen(hex);
            d.seen = 1;
            g_devs.push_back(d);
        }
        if (g_mtx) xSemaphoreGive(g_mtx);
    }
};

void scanTask(void *) {
    if (!g_bleInit) {
        BLEDevice::init("IncognitoX");
        g_bleInit = true;
    }
    BLEScan *sc = BLEDevice::getScan();
    sc->setAdvertisedDeviceCallbacks(new AdvCb(), true);
    sc->setActiveScan(true);
    sc->setInterval(90);
    sc->setWindow(50);
    BLEScanResults *res = sc->start(g_seconds, false);
    if (res) sc->clearResults();
    g_running = false;
    g_done = true;
    vTaskDelete(nullptr);
}

void startScan(uint32_t seconds) {
    if (g_running) return;
    g_seconds = seconds;
    g_done = false;
    g_running = true;
    if (!g_mtx) g_mtx = xSemaphoreCreateMutex();
    if (g_mtx) {
        xSemaphoreTake(g_mtx, portMAX_DELAY);
        g_devs.clear();
        xSemaphoreGive(g_mtx);
    }
    xTaskCreatePinnedToCore(scanTask, "blescan", 6144, nullptr, 2, nullptr, 0);
}

class BleDetail : public Screen {
public:
    explicit BleDetail(const BleDev &d) : d_(d) {}
    const char *title() const override { return "BLE устройство"; }
    const char *hint() const override { return "< Назад   OK сохранить в SD"; }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        int y = UI_CONTENT_Y0 + 8;
        gfx::fillRoundRect(8, y, 224, 52, 8, gfx::col::PANEL);
        gfx::drawIcon(d_.tracker ? gfx::Icon::Search : gfx::Icon::Bluetooth, 16,
                      y + 12, 28, gfx::col::accent);
        char nm[30];
        gfx::ellipsize(nm, sizeof(nm), d_.name[0] ? d_.name : "(no name)", 180,
                       gfx::F_BODY_B);
        gfx::text(54, y + 24, nm, gfx::F_BODY_B, gfx::col::TEXT, gfx::col::PANEL);
        gfx::text(54, y + 42, d_.tracker ? "возможен трекер" : "BLE рекламодатель",
                  gfx::F_SMALL,
                  d_.tracker ? gfx::col::WARN : gfx::col::DIM, gfx::col::PANEL);

        y += 60;
        const char *cn = companyName(d_.company);
        char co[32];
        if (cn) snprintf(co, sizeof(co), "%s (0x%04X)", cn, d_.company);
        else snprintf(co, sizeof(co), "0x%04X", d_.company);

        struct Row { const char *k; String v; };
        Row rows[] = {
            {"Address", String(d_.addr)},
            {"RSSI", String(d_.rssi) + " dBm"},
            {"Vendor", String(co)},
            {"Connectable", d_.connectable ? "yes" : "no"},
            {"Packets seen", String(d_.seen)},
        };
        int i = 0;
        for (auto &r : rows) {
            uint16_t rowBg = (i++ % 2) ? bg : gfx::col::BG2;
            gfx::fillRect(8, y, 224, 20, rowBg);
            gfx::text(14, y + 15, r.k, gfx::F_SMALL, gfx::col::DIM, rowBg);
            gfx::textRight(220, y + 15, r.v.c_str(), gfx::F_SMALL, gfx::col::TEXT,
                           rowBg);
            y += 20;
        }

        y += 4;
        gfx::text(12, y + 12, "Объявление (сырое):", gfx::F_SMALL, gfx::col::DIM, bg);
        y += 18;

        for (int off = 0; off < d_.advLen && off < 64; off += 32) {
            char chunk[36];
            strncpy(chunk, d_.adv + off, 32);
            chunk[32] = 0;
            gfx::text5(12, y, chunk, gfx::col::TEXT2, bg);
            y += 11;
        }
    }

    bool onKey(Key k, KeyEvent e) override {
        if (k == Key::Ok && e == KeyEvent::Press) {
            if (!storage::mounted()) { app::toast("no microSD"); return true; }
            storage::ensureDir("/logs");
            char line[160];
            snprintf(line, sizeof(line), "%s,%s,%d,%s,%s", d_.addr,
                     d_.name[0] ? d_.name : "-", d_.rssi,
                     d_.tracker ? "tracker" : "device", d_.adv);
            storage::appendLine("/logs/ble_devices.csv", line);
            app::toast("saved");
            return true;
        }
        return false;
    }

private:
    BleDev d_;
};

class BleScan : public Screen {
public:
    const char *title() const override { return "Сканер BLE"; }
    const char *hint() const override { return "< Назад  OK инфо  > повторно сканировать"; }

    void enter() override { startScan(6); }

    void tick() override {
        if (g_done) {
            g_done = false;
            g_running = false;
            klog::add("ble: %d devices", (int)g_devs.size());
            app::invalidate();
        }
    }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        if (g_running) {
            gfx::text(12, UI_CONTENT_Y0 + 30, "Поиск BLE...", gfx::F_BODY_B,
                      gfx::col::accent, bg);
            for (int i = 0; i < 5; i++) {
                int r = 8 + ((millis() / 90 + i * 12) % 40);
                gfx::circle(120, 140, r, gfx::col::accentDim());
            }
            char b[48];
            snprintf(b, sizeof(b), "found: %d", (int)g_devs.size());
            gfx::text(12, 196, b, gfx::F_SMALL, gfx::col::DIM, bg);
            return;
        }
        if (g_devs.empty()) {
            gfx::text(12, UI_CONTENT_Y0 + 30, "BLE: не найдено", gfx::F_BODY_B,
                      gfx::col::DIM, bg);
            return;
        }

        const int rowH = 30, visible = 6;
        if (cursor_ < scroll_) scroll_ = cursor_;
        if (cursor_ >= scroll_ + visible) scroll_ = cursor_ - visible + 1;

        for (int i = 0; i < visible; i++) {
            int idx = scroll_ + i;
            if (idx >= (int)g_devs.size()) break;
            const BleDev &d = g_devs[idx];
            int y = UI_CONTENT_Y0 + 2 + i * rowH;
            bool sel = (idx == cursor_);
            uint16_t rowBg = sel ? gfx::col::PANEL2 : bg;
            if (sel) {
                gfx::fillRoundRect(4, y, 232, rowH - 2, 5, gfx::col::PANEL2);
                gfx::fillRect(4, y + 4, 3, rowH - 10, gfx::col::accent);
            }
            gfx::drawIcon(d.tracker ? gfx::Icon::Search : gfx::Icon::Bluetooth, 12,
                          y + 5, 18,
                          d.tracker ? gfx::col::WARN : gfx::col::accent);
            char nm[26];
            gfx::ellipsize(nm, sizeof(nm), d.name[0] ? d.name : d.addr, 170,
                           gfx::F_SMALL);
            gfx::text(36, y + 13, nm, gfx::F_SMALL, gfx::col::TEXT, rowBg);
            char sub[24];
            snprintf(sub, sizeof(sub), "%s%s", d.addr,
                     d.tracker ? "  tracker" : "");
            gfx::text5(36, y + 18, sub, d.tracker ? gfx::col::WARN : gfx::col::DIM,
                       rowBg);
            char r[8];
            snprintf(r, sizeof(r), "%d", d.rssi);
            gfx::textRight(220, y + 13, r, gfx::F_SMALL, gfx::col::TEXT2, rowBg);
            gfx::hline(8, y + rowH - 3, 224, gfx::col::PANEL);
        }
    }

    bool onKey(Key k, KeyEvent e) override {
        if (k == Key::Right && e == KeyEvent::Press) {
            cursor_ = scroll_ = 0;
            startScan(6);
            return true;
        }
        if (g_running || g_devs.empty()) return false;
        if (k == Key::Up && (e == KeyEvent::Press || e == KeyEvent::Repeat)) {
            cursor_ = (cursor_ - 1 + (int)g_devs.size()) % (int)g_devs.size();
            return true;
        }
        if (k == Key::Down && (e == KeyEvent::Press || e == KeyEvent::Repeat)) {
            cursor_ = (cursor_ + 1) % (int)g_devs.size();
            return true;
        }
        if (k == Key::Ok && e == KeyEvent::Press) {
            if (g_mtx) xSemaphoreTake(g_mtx, portMAX_DELAY);
            BleDev copy = g_devs[cursor_];
            if (g_mtx) xSemaphoreGive(g_mtx);
            app::push(new BleDetail(copy));
            return true;
        }
        return false;
    }

private:
    int cursor_ = 0;
    int scroll_ = 0;
};

}

namespace screens {

void openBle() {
    std::vector<MenuItem> items;
    items.push_back({"Сканировать устройства", gfx::Icon::Search, "", true,
                     [] { app::push(new BleScan()); }});
    items.push_back({"BLE-спам", gfx::Icon::Lock, "", false,
                     [] { app::push(new LockedScreen(
                              "BLE spam",
                              "Flooding iPhones/Androids with fake pairing popups is a "
                              "denial-of-service prank. Not implemented.",
                              "Scanning, tracker detection and advertisement analysis are "
                              "fully available and harmless.")); }});
    items.push_back({"Сниффинг объявлений", gfx::Icon::Bluetooth, "", true,
                     [] { app::push(new BleScan()); }});
    app::push(new ListScreen("Bluetooth", std::move(items)));
}

}
