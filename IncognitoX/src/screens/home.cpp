#include <Arduino.h>

#include "app/app.h"
#include "app/settings.h"
#include "app/ui.h"
#include "config.h"
#include "hw/buzzer.h"
#include "hw/gps.h"
#include "hw/gfx.h"
#include "hw/power.h"
#include "hw/selftest.h"
#include "hw/storage.h"
#include "pins.h"
#include "screens/screens.h"

namespace {

class AboutScreen : public Screen {
public:
    const char *title() const override { return "О программе"; }
    const char *hint() const override { return "< back"; }
    void draw() override {
        const uint16_t bg = gfx::col::BG;
        int y = UI_CONTENT_Y0 + 10;

        gfx::fillRoundRect(8, y, 224, 62, 8, gfx::col::PANEL);
        gfx::text(20, y + 30, "IncognitoX", gfx::F_BIG_B, gfx::col::accent,
                  gfx::col::PANEL);
        gfx::text(20, y + 52, "S3 multi-tool  v" IX_VERSION_STR, gfx::F_SMALL,
                  gfx::col::TEXT2, gfx::col::PANEL);
        gfx::drawIcon(gfx::Icon::Chip, 212, y + 17, 28, gfx::col::accent);

        y += 74;
        char buf[80];
        snprintf(buf, sizeof(buf), "Чип:   %s rev %d, %d МГц",
                 ESP.getChipModel(), ESP.getChipRevision(), getCpuFrequencyMhz());
        gfx::text(12, y + 10, buf, gfx::F_SMALL, gfx::col::TEXT2, bg);
        y += 18;
        snprintf(buf, sizeof(buf), "Флеш:  %lu МБ   ОЗУ: %lu КБ",
                 (unsigned long)(ESP.getFlashChipSize() / (1024 * 1024)),
                 (unsigned long)(ESP.getFreeHeap() / 1024));
        gfx::text(12, y + 10, buf, gfx::F_SMALL, gfx::col::TEXT2, bg);
        y += 18;
        snprintf(buf, sizeof(buf), "SD:     %s %lu МБ",
                 storage::mounted() ? storage::cardType().c_str() : "нет",
                 (unsigned long)(storage::totalBytes() / (1024ULL * 1024ULL)));
        gfx::text(12, y + 10, buf, gfx::F_SMALL, gfx::col::TEXT2, bg);
        y += 18;
        snprintf(buf, sizeof(buf), "Время: %lu мин", (unsigned long)(millis() / 60000));
        gfx::text(12, y + 10, buf, gfx::F_SMALL, gfx::col::TEXT2, bg);

        y += 26;
        gfx::hline(12, y, 216, gfx::col::LINE);
        y += 6;
        gfx::text(12, y + 12, "Клавиши: стрелки и OK", gfx::F_SMALL,
                  gfx::col::DIM, bg);
        y += 22;
        gfx::text(12, y + 12, "Слева = назад,", gfx::F_SMALL,
                  gfx::col::DIM, bg);
        y += 22;
        gfx::text(12, y + 8, "OK = выбор", gfx::F_SMALL, gfx::col::DIM, bg);
    }
    bool onKey(Key k, KeyEvent e) override {
        (void)k;
        (void)e;
        return false;
    }
};

}

namespace screens {

void openHome() {
    std::vector<MenuItem> items;

    items.push_back({"Wi-Fi", gfx::Icon::Wifi, "", true, [] { openWifi(); }});
    items.push_back({"Bluetooth", gfx::Icon::Bluetooth, "", true, [] { openBle(); }});
    items.push_back({"Sub-GHz 433", gfx::Icon::SubGhz, "", true, [] { openSubGhz(); }});
    items.push_back({"Сканер 2.4 GHz", gfx::Icon::Nrf, "", true, [] { openNrf(); }});
    items.push_back({"NFC / RFID", gfx::Icon::Nfc, "", true, [] { openNfc(); }});
    items.push_back({"ИК", gfx::Icon::Ir, "", true, [] { openIr(); }});
    items.push_back({"GPS / Map", gfx::Icon::Gps, "", true, [] { openGps(); }});
    items.push_back({"Онлайн", gfx::Icon::Web, "", true, [] { openOnline(); }});
    items.push_back({"Игры", gfx::Icon::Game, "", true, [] { openGames(); }});
    items.push_back({"Файлы", gfx::Icon::Folder, "", true, [] { openFiles(); }});
    items.push_back({"Система", gfx::Icon::Chip, "", true, [] { openSystem(); }});
    items.push_back({"Настройки", gfx::Icon::Gear, "", true, [] { openSettings(); }});
    items.push_back({"О программе", gfx::Icon::Info, "", true, [] { openAbout(); }});

    app::push(new ListScreen("IncognitoX", std::move(items),
                             "< Назад   OK открыть   ^v движение"));
}

void openAbout() { app::push(new AboutScreen()); }

}
