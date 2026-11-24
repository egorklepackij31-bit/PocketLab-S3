#include <vector>

#include "app/app.h"
#include "app/log.h"
#include "app/ui.h"
#include "config.h"
#include "hw/buzzer.h"
#include "hw/gfx.h"
#include "hw/input.h"
#include "hw/storage.h"
#include "lib/pn532.h"
#include "lib/rc522.h"
#include "screens/screens.h"

namespace {

#define NFC_DIR "/nfc"

String uidHex(const uint8_t *uid, uint8_t len) {
    String s;
    for (uint8_t i = 0; i < len; i++) {
        char b[4];
        snprintf(b, sizeof(b), i ? ":%02X" : "%02X", uid[i]);
        s += b;
    }
    return s;
}

const char *sakName(uint8_t sak) {
    switch (sak) {
    case 0x00: return "NTAG / Ultralight";
    case 0x08: return "MIFARE Classic 1K";
    case 0x09: return "MIFARE Mini";
    case 0x18: return "MIFARE Classic 4K";
    case 0x20: return "ISO14443-4 (DESFire)";
    case 0x28: return "SmartMX / JCOP";
    default: return "unknown PICC";
    }
}

class Pn532Read : public Screen {
public:
    const char *title() const override { return "Информация PN532"; }
    const char *hint() const override { return "< назад   OK UID"; }

    void enter() override {
        if (!pn532.present())
            app::message("PN532", "Не отвечает на I2C 0x24. SDA=40, SCL=41.");

        lastPoll_ = 0;
    }

    void tick() override {
        if (!pn532.present()) return;
        if (millis() - lastPoll_ < 350) return;
        lastPoll_ = millis();
        NfcTag t;
        bool got = pn532.readTagA(t, 50);
        if (got && t.valid) {
            if (!tag_.valid || memcmp(tag_.uid, t.uid, t.uidLen) != 0) {
                buzzer::ok();
                tag_ = t;
                missCount_ = 0;
                klog::add("nfc tag: %s (%02X)", uidHex(tag_.uid, tag_.uidLen).c_str(),
                          tag_.selRes);
                app::invalidate();
            } else {
                tag_ = t;
                missCount_ = 0;
            }
        } else if (tag_.valid && ++missCount_ > 5) {
            tag_.valid = false;
            app::invalidate();
        }
    }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        int y = UI_CONTENT_Y0 + 6;

        gfx::fillRoundRect(8, y, 224, 50, 8, gfx::col::PANEL);
        gfx::drawIcon(gfx::Icon::Nfc, 18, y + 10, 30,
                      pn532.present() ? gfx::col::accent : gfx::col::ERR);
        gfx::text(60, y + 22, "PN532 I2C 0x24", gfx::F_BODY_B, gfx::col::TEXT,
                  gfx::col::PANEL);
        gfx::text(60, y + 40,
                  pn532.present() ? "опрос ISO14443A..." : "читатель не найден",
                  gfx::F_SMALL,
                  pn532.present() ? gfx::col::DIM : gfx::col::ERR, gfx::col::PANEL);

        y += 62;
        if (!tag_.valid) {
            gfx::drawIcon(gfx::Icon::Search, 146, y + 22, 28, gfx::col::DIM2);
            drawWrapped("Положите карту на антену PN532", 10, y + 57, 216, 16,
                        gfx::col::DIM, bg, gfx::F_SMALL);
            return;
        }

        gfx::text(12, y + 14, "UID", gfx::F_SMALL, gfx::col::DIM, bg);
        gfx::text(12, y + 42, uidHex(tag_.uid, tag_.uidLen).c_str(), gfx::F_BODY_B,
                  gfx::col::accent, bg);

        y += 54;
        char buf[72];
        snprintf(buf, sizeof(buf), "Type   %s", sakName(tag_.selRes));
        gfx::text(12, y + 12, buf, gfx::F_SMALL, gfx::col::TEXT2, bg);
        snprintf(buf, sizeof(buf), "ATQA   0x%04X    SAK 0x%02X", tag_.sensRes,
                 tag_.selRes);
        gfx::text(12, y + 30, buf, gfx::F_SMALL, gfx::col::TEXT2, bg);
        snprintf(buf, sizeof(buf), "UID length  %u byte(s)", tag_.uidLen);
        gfx::text(12, y + 48, buf, gfx::F_SMALL, gfx::col::DIM, bg);
    }

    bool onKey(Key k, KeyEvent e) override {
        if (k == Key::Ok && e == KeyEvent::Press) {
            if (!tag_.valid) return true;
            if (!storage::mounted()) { app::toast("no microSD"); return true; }
            storage::ensureDir(NFC_DIR);
            char path[64];
            snprintf(path, sizeof(path), NFC_DIR "/uid_%s.txt",
                     uidHex(tag_.uid, tag_.uidLen).c_str());
            String txt = "IncognitoX NFC tag\nreader=PN532\nuid=" +
                         uidHex(tag_.uid, tag_.uidLen) + "\n";
            char b[80];
            snprintf(b, sizeof(b), "atqa=0x%04X\nsak=0x%02X\ntype=%s\n",
                     tag_.sensRes, tag_.selRes, sakName(tag_.selRes));
            txt += b;
            storage::writeText(path, txt.c_str());
            app::toast("saved %s", path);
            return true;
        }
        return false;
    }

private:
    NfcTag   tag_;
    uint32_t lastPoll_ = 0;
    uint8_t  missCount_ = 0;
};

class Rc522Dump;

class Rc522Read : public Screen {
public:
    const char *title() const override { return "Информация RC522"; }
    const char *hint() const override { return "< back   hold OK save UID"; }

    void enter() override {
        if (!rc522.present())
            app::message("RC522", "Не отвечает на SPI. Производится CS=21, RST=42 и 3.3 V.");
        lastPoll_ = 0;
    }

    void leave() override { rc522.antennaOn(false); }

    void tick() override {
        if (!rc522.present()) return;
        if (millis() - lastPoll_ < 320) return;
        lastPoll_ = millis();
        RfidCard c;
        if (rc522.scan(c, 40) && c.valid) {
            if (!card_.valid || memcmp(card_.uid, c.uid, max(card_.uidLen, c.uidLen)) != 0) {
                buzzer::ok();
                card_ = c;
                miss_ = 0;
                klog::add("rfid: %s sak %02X", uidHex(c.uid, c.uidLen).c_str(),
                          c.sak);
                app::invalidate();
            } else {
                card_ = c;
                miss_ = 0;
            }
        } else if (card_.valid && ++miss_ > 5) {
            card_.valid = false;
            app::invalidate();
        }
    }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        int y = UI_CONTENT_Y0 + 6;

        gfx::fillRoundRect(8, y, 224, 50, 8, gfx::col::PANEL);
        gfx::drawIcon(gfx::Icon::Key, 18, y + 10, 30,
                      rc522.present() ? gfx::col::accent : gfx::col::ERR);
        gfx::text(48, y + 22, "RC522 13.56MHz", gfx::F_BODY_B, gfx::col::TEXT,
                  gfx::col::PANEL);
        gfx::text(60, y + 40,
                  rc522.present() ? "опрос ISO14443A..." : "читатель не найден",
                  gfx::F_SMALL,
                  rc522.present() ? gfx::col::DIM : gfx::col::ERR, gfx::col::PANEL);

        y += 62;
        if (!card_.valid) {
            gfx::drawIcon(gfx::Icon::Search, 146, y + 22, 28, gfx::col::DIM2);
            drawWrapped("Положите карту на антену RC522", 10, y + 57, 216, 16,
                        gfx::col::DIM, bg, gfx::F_SMALL);
            return;
        }

        gfx::text(12, y + 14, "UID", gfx::F_SMALL, gfx::col::DIM, bg);
        gfx::text(12, y + 42, uidHex(card_.uid, card_.uidLen).c_str(), gfx::F_BODY_B,
                  gfx::col::accent, bg);

        y += 54;
        char buf[72];
        snprintf(buf, sizeof(buf), "Type   %s", rc522.typeName(card_.sak));
        gfx::text(12, y + 12, buf, gfx::F_SMALL, gfx::col::TEXT2, bg);
        snprintf(buf, sizeof(buf), "ATQA   0x%04X    SAK 0x%02X", card_.atqa,
                 card_.sak);
        gfx::text(12, y + 30, buf, gfx::F_SMALL, gfx::col::TEXT2, bg);
        drawWrapped(card_.sak == 0x00 ? "Открытны поскои можно дамбити"
                             : "Дамб только для NTAG/Ultralight",
                    12, y + 48, 216, 16, gfx::col::DIM, bg, gfx::F_SMALL);
    }

    bool onKey(Key k, KeyEvent e) override;

private:
    RfidCard card_;
    uint32_t lastPoll_ = 0;
    uint8_t  miss_ = 0;

    void saveUid() {
        if (!storage::mounted()) { app::toast("no microSD"); return; }
        storage::ensureDir(NFC_DIR);
        char path[64];
        snprintf(path, sizeof(path), NFC_DIR "/rc522_%s.txt",
                 uidHex(card_.uid, card_.uidLen).c_str());
        char txt[160];
        snprintf(txt, sizeof(txt),
                 "IncognitoX RC522 card\nuid=%s\natqa=0x%04X\nsak=0x%02X\ntype=%s\n",
                 uidHex(card_.uid, card_.uidLen).c_str(), card_.atqa, card_.sak,
                 rc522.typeName(card_.sak));
        storage::writeText(path, txt);
        app::toast("saved %s", path);
    }
};

class Rc522Dump : public Screen {
public:
    const char *title() const override { return "Дамп RC522"; }
    const char *hint() const override { return "< back   OK save dump"; }

    void enter() override {
        for (auto &l : lines_) l = "";
        card_.valid = false;
        phase_ = Phase::Waiting;
        next_ = 0;
    }

    void tick() override {
        if (millis() < next_) return;
        next_ = millis() + 300;
        if (phase_ == Phase::Waiting) {
            RfidCard c;
            if (rc522.scan(c, 40) && c.valid) {
                card_ = c;
                if (c.sak != 0x00) {
                    phase_ = Phase::Unsupported;
                } else {
                    phase_ = Phase::Reading;
                    app::invalidate();
                }
            }
            return;
        }
        if (phase_ == Phase::Reading) {
            readAll();
            phase_ = ok_ ? Phase::Done : Phase::Failed;
            app::invalidate();
        }
    }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        if (phase_ == Phase::Waiting) {
            gfx::drawIcon(gfx::Icon::Search, 146, UI_CONTENT_Y0 + 40, 28,
                          gfx::col::DIM2);
            gfx::text(10, UI_CONTENT_Y0 + 100, "Ожидание NTAG...", gfx::F_SMALL, gfx::col::DIM,
                      bg);
            return;
        }
        if (phase_ == Phase::Unsupported) {
            drawWrapped("Это не карта NTAG/Ultralight", 12, UI_CONTENT_Y0 + 17, 216, 24,
                        gfx::col::WARN, bg, gfx::F_BODY_B);
            drawWrapped("Читается только открытая память тегов SAK 0x00.",
                        12, UI_CONTENT_Y0 + 50, 216, 16, gfx::col::DIM, bg, gfx::F_SMALL);
            return;
        }
        if (phase_ == Phase::Failed) {
            drawWrapped("Чтение не удалось", 12, UI_CONTENT_Y0 + 34, 216, 20, gfx::col::ERR,
                        bg, gfx::F_BODY_B);
            drawWrapped("Держите карту неподвижно и попробуйте снова.", 12,
                        UI_CONTENT_Y0 + 50, 216, 16, gfx::col::DIM, bg, gfx::F_SMALL);
            return;
        }

        gfx::text(8, UI_CONTENT_Y0 + 12, uidHex(card_.uid, card_.uidLen).c_str(),
                  gfx::F_SMALL, gfx::col::accent, bg);
        int y = UI_CONTENT_Y0 + 20;
        for (size_t i = 0; i < sizeof(lines_) / sizeof(lines_[0]) && y < UI_CONTENT_Y1 - 6;
             i++) {
            gfx::text5(8, y, lines_[i].c_str(), gfx::col::TEXT2, bg);
            y += 11;
        }
    }

    bool onKey(Key k, KeyEvent e) override {
        if (k == Key::Ok && e == KeyEvent::Press && phase_ == Phase::Done) {
            save();
            return true;
        }
        return false;
    }

private:
    enum class Phase { Waiting, Reading, Done, Failed, Unsupported };
    Phase   phase_ = Phase::Waiting;
    RfidCard card_;
    String  lines_[17];
    bool    ok_ = false;
    uint32_t next_ = 0;

    void readAll() {
        ok_ = true;
        lines_[0] = String("page  data             ascii");
        for (int b = 0; b < 16; b++) {
            if (b % 4 == 0) {
                uint8_t pg[16];
                if (!rc522.readPages((uint8_t)b, pg)) {
                    ok_ = false;
                    lines_[b + 1] = String("-- read error at page ") + b;
                    continue;
                }
                for (int p = 0; p < 4; p++) {
                    if (b + p >= 16) break;
                    char line[64];
                    char asc[6] = {0};
                    for (int i = 0; i < 4; i++) {
                        uint8_t v = pg[p * 4 + i];
                        asc[i] = (v >= 32 && v < 127) ? (char)v : '.';
                    }
                    snprintf(line, sizeof(line), "P%02d   %02X %02X %02X %02X      %s",
                             b + p, pg[p * 4], pg[p * 4 + 1], pg[p * 4 + 2],
                             pg[p * 4 + 3], asc);
                    lines_[b + p + 1] = line;
                }
            }
        }
    }

    void save() {
        if (!storage::mounted()) { app::toast("no microSD"); return; }
        storage::ensureDir(NFC_DIR);
        char path[64];
        snprintf(path, sizeof(path), NFC_DIR "/ntag_%s.txt",
                 uidHex(card_.uid, card_.uidLen).c_str());
        String txt = "IncognitoX NTAG/Ultralight dump\nuid=" +
                     uidHex(card_.uid, card_.uidLen) +
                     "\nnote=open pages only, no key recovery\n\n";
        for (auto &l : lines_) txt += l + "\n";
        storage::writeText(path, txt.c_str());
        app::toast("saved %s", path);
        klog::add("nfc dump saved: %s", path);
    }
};

bool Rc522Read::onKey(Key k, KeyEvent e) {
    if (k == Key::Ok) {
        if (e == KeyEvent::Press && card_.valid && card_.sak == 0x00) {
            app::push(new Rc522Dump());
            return true;
        }
        if (e == KeyEvent::Long && card_.valid) {
            saveUid();
            return true;
        }
    }
    return false;
}

class ReadersInfo : public Screen {
public:
    const char *title() const override { return "Статус NFC"; }
    const char *hint() const override { return "< back"; }
    void draw() override {
        const uint16_t bg = gfx::col::BG;
        int y = UI_CONTENT_Y0 + 14;
        char b[80];
        gfx::text(12, y, "PN532 (I2C 0x24)", gfx::F_SMALL_B, gfx::col::accent, bg);
        y += 18;
        if (pn532.present()) {
            uint32_t fw = pn532.firmware();
            snprintf(b, sizeof(b), "firmware %06lX   IC %lu", (unsigned long)(fw >> 8),
                     (unsigned long)((fw >> 24) & 0xFF));
            gfx::text5(12, y, b, gfx::col::TEXT2, bg);
        } else {
            gfx::text5(12, y, "not detected", gfx::col::ERR, bg);
        }
        y += 26;
        gfx::text(12, y, "RC522 (SPI 6 MHz)", gfx::F_SMALL_B, gfx::col::accent, bg);
        y += 18;
        if (rc522.present()) {
            snprintf(b, sizeof(b), "version register 0x%02X", rc522.version());
            gfx::text5(12, y, b, gfx::col::TEXT2, bg);
        } else {
            gfx::text5(12, y, "not detected", gfx::col::ERR, bg);
        }
        y += 30;
        gfx::hline(12, y, 224, gfx::col::LINE);
        y += 8;
        drawWrapped("Поддерживается UID ISO14443A и дамп NTAG/Ultralight.", 12, y + 12,
                    216, 16, gfx::col::DIM, bg, gfx::F_SMALL);
        y += 34;
        drawWrapped("Секторам MIFARE Classic нужны ключи, доступ запрещён.", 12, y + 12,
                    216, 16, gfx::col::WARN, bg, gfx::F_SMALL);
    }
    bool onKey(Key, KeyEvent) override { return false; }
};

}

namespace screens {

void openNfc() {
    std::vector<MenuItem> items;
    items.push_back({"Информация PN532", gfx::Icon::Nfc, "", true,
                     [] { app::push(new Pn532Read()); }});
    items.push_back({"Информация RC522", gfx::Icon::Key, "", true,
                     [] { app::push(new Rc522Read()); }});
    items.push_back({"Дамп NTAG/Ultralight", gfx::Icon::Search, "", true,
                     [] { app::push(new Rc522Dump()); }});
    items.push_back({"Статус NFC", gfx::Icon::Info, "", true,
                     [] { app::push(new ReadersInfo()); }});
    items.push_back({"Экспорт ключей MIFARE", gfx::Icon::Lock, "", false,
                     [] { app::push(new LockedScreen(
                              "MIFARE key recovery",
                              "Nested/darkside attacks and key dictionaries break "
                              "access control on cards you do not own. This firmware "
                              "does not implement them.",
                              "Read UIDs of your own tags, and dump open NTAG/"
                              "Ultralight pages for inventory or backup.")); }});
    items.push_back({"Клонирование и эмуляция", gfx::Icon::Lock, "", false,
                     [] { app::push(new LockedScreen(
                              "Card cloning and emulation",
                              "Cloning an access card lets you enter places without "
                              "permission, so it is deliberately absent.",
                              "PN532 and RC522 identify and catalogue your own cards; "
                              "use the issuer's official process for duplicates.")); }});
    items.push_back({"Запись NTAG/Ultralight", gfx::Icon::Lock, "", false,
                     [] { app::push(new LockedScreen(
                              "Writing NFC tags",
                              "Blind writes can brick a tag and are easy to misuse on "
                              "foreign cards. The app is read-only by design.",
                              "Use a phone app or the tag vendor's tool when you "
                              "actually need to write your own tag.")); }});
    app::push(new ListScreen("NFC / RFID", std::move(items),
                             "< back   OK open   ^v move"));
}

}


