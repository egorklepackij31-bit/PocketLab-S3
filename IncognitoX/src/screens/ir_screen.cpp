#include <vector>

#include "app/app.h"
#include "app/log.h"
#include "app/ui.h"
#include "config.h"
#include "hw/buzzer.h"
#include "hw/gfx.h"
#include "hw/input.h"
#include "hw/storage.h"
#include "lib/ir.h"
#include "screens/screens.h"

namespace {

IrCode g_codes[MAX_IR_CODES];
int    g_codeCount = 0;

void storeCode(const IrCode &c) {
    if (!c.valid) return;

    for (int i = 0; i < g_codeCount; i++)
        if (g_codes[i].data == c.data && g_codes[i].proto == c.proto) return;
    if (g_codeCount < MAX_IR_CODES) {
        g_codes[g_codeCount++] = c;
    } else {
        memmove(&g_codes[0], &g_codes[1], sizeof(IrCode) * (MAX_IR_CODES - 1));
        g_codes[MAX_IR_CODES - 1] = c;
    }
}

void sendForDevice(int device, uint32_t data, uint8_t bits) {
    if (device == 0) ir.sendSamsung(data, bits ? bits : 32);
    else ir.sendNec(data, bits ? bits : 32);
}

class IrReceive : public Screen {
public:
    const char *title() const override { return "ИК-приёмник"; }
    const char *hint() const override { return "< Назад   OK сохранить   удержание OK записать"; }

    void enter() override {
        last_ = IrCode();
        raw_ = 0;
        listening_ = true;
    }

    void tick() override {
        IrCode c;
        if (ir.poll(c)) {
            raw_ = 0;
            if (c.valid) {
                if (!last_.valid || last_.data != c.data || last_.proto != c.proto) {
                    buzzer::ok();
                    last_ = c;
                    storeCode(c);
                    klog::add("ir: %s %08lX (%u bits)", irProtoName(c.proto),
                              (unsigned long)c.data, c.bits);
                }
                app::invalidate();
            }
        }
        if (ir.receiving()) raw_ = ir.rawEdges();
        if (ir.receiving() && millis() - lastDraw_ > 250) {
            lastDraw_ = millis();
            app::invalidate();
        }
    }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        int y = UI_CONTENT_Y0 + 6;

        gfx::fillRoundRect(8, y, 224, 46, 8, gfx::col::PANEL);
        gfx::drawIcon(gfx::Icon::Ir, 18, y + 10, 26,
                      last_.valid ? gfx::col::accent : gfx::col::DIM);
        gfx::text(58, y + 20,
                  last_.valid ? irProtoName(last_.proto) : "нет сигнала",
                  gfx::F_BODY_B,
                  last_.valid ? gfx::col::TEXT : gfx::col::DIM, gfx::col::PANEL);
        gfx::text(58, y + 38, raw_ > 4 ? "декодируем..." : "наведите пульт",
                  gfx::F_SMALL, gfx::col::DIM, gfx::col::PANEL);

        y += 56;
        if (last_.valid) {
            char b[48];
            snprintf(b, sizeof(b), "0x%08lX", (unsigned long)last_.data);
            gfx::text(12, y + 26, b, gfx::F_BIG_B, gfx::col::accent, bg);
            snprintf(b, sizeof(b), "%u bits   %s   %d in session", last_.bits,
                     irProtoName(last_.proto), g_codeCount);
            gfx::text(12, y + 46, b, gfx::F_SMALL, gfx::col::TEXT2, bg);
            snprintf(b, sizeof(b), "raw edges: %d", raw_);
            gfx::text(12, y + 64, b, gfx::F_SMALL, gfx::col::DIM, bg);
        } else {
            gfx::text(12, y + 30, "Код ещё не принят", gfx::F_BODY_B,
                      gfx::col::DIM, bg);
            drawWrapped("Протоколы NEC и Samsung декодируются.", 12, y + 41, 216, 16,
                        gfx::col::DIM, bg, gfx::F_SMALL);
        }

        drawRaw(y + 74);
    }

    bool onKey(Key k, KeyEvent e) override {
        if (k == Key::Ok) {
            if (e == KeyEvent::Press && last_.valid) {
                storeCode(last_);
                app::toast("code stored (%d)", g_codeCount);
                return true;
            }
            if (e == KeyEvent::Long && last_.valid) {
                saveSd(last_);
                return true;
            }
        }
        return false;
    }

private:
    IrCode   last_;
    int      raw_ = 0;
    bool     listening_ = false;
    uint32_t lastDraw_ = 0;

    void drawRaw(int top) {
        uint32_t d[IR_MAX_EDGES];
        int n = ir.durations(d, IR_MAX_EDGES);
        gfx::text(10, top + 10, "огибающая импульса", gfx::F_SMALL, gfx::col::DIM, gfx::col::BG);
        if (n < 4) return;
        const int x0 = 10, x1 = 220, mid = top + 42;
        uint32_t maxUs = 1;
        for (int i = 0; i < n; i++)
            if (d[i] > maxUs) maxUs = d[i];
        int px = x0;
        bool high = true;
        for (int i = 0; i < n && px < x1; i++) {
            int w = (int)((int64_t)(x1 - x0) * d[i] / maxUs);
            if (w < 1) w = 1;
            gfx::fillRect(px, high ? mid - 18 : mid, w, 18,
                          high ? gfx::col::accent : gfx::col::PANEL3);
            px += w;
            high = !high;
        }
    }

    void saveSd(const IrCode &c) {
        if (!storage::mounted()) { app::toast("no microSD"); return; }
        storage::ensureDir(IR_DIR);
        char path[64];
        snprintf(path, sizeof(path), IR_DIR "/%s_%08lX.txt",
                 c.proto == IR_SAMSUNG ? "samsung" : "nec",
                 (unsigned long)c.data);
        char txt[160];
        snprintf(txt, sizeof(txt),
                 "# IncognitoX IR code\nprotocol=%s\ncode=0x%08lX\nbits=%u\n",
                 irProtoName(c.proto), (unsigned long)c.data, c.bits);
        storage::writeText(path, txt);
        app::toast("saved %s", path);
    }
};

class IrRemote : public Screen {
public:
    const char *title() const override { return "Универсальный пульт"; }
    const char *hint() const override { return "< Назад   OK отправить   ^v кнопка"; }

    void enter() override { load(device_); }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        gfx::text(10, UI_CONTENT_Y0 + 14, "ИК-светодиод на GPIO15", gfx::F_SMALL,
                  gfx::col::DIM, bg);

        gfx::fillRoundRect(8, UI_CONTENT_Y0 + 20, 224, 30, 6, gfx::col::PANEL);
        gfx::text(16, UI_CONTENT_Y0 + 40, IrPort::tvName(device_), gfx::F_BODY_B,
                  gfx::col::accent, gfx::col::PANEL);
        char b[32];
        snprintf(b, sizeof(b), "device %d / %d", device_ + 1, IrPort::tvCount());
        gfx::textRight(220, UI_CONTENT_Y0 + 40, b, gfx::F_SMALL, gfx::col::DIM,
                       gfx::col::PANEL);

        if (!buttons_ || count_ == 0) return;
        const int rowH = 26;
        int top = UI_CONTENT_Y0 + 56;
        int visible = (UI_CONTENT_Y1 - top - 4) / rowH;
        if (cursor_ < scroll_) scroll_ = cursor_;
        if (cursor_ >= scroll_ + visible) scroll_ = cursor_ - visible + 1;
        for (int i = 0; i < visible; i++) {
            int idx = scroll_ + i;
            if (idx >= count_) break;
            int y = top + i * rowH;
            bool sel = idx == cursor_;
            if (sel) gfx::fillRoundRect(6, y, 228, rowH - 2, 5, gfx::col::PANEL2);
            gfx::drawIcon(gfx::Icon::Play, 14, y + 6, 12,
                          sel ? gfx::col::accent : gfx::col::DIM);
            gfx::text(34, y + 17, buttons_[idx].name, gfx::F_SMALL,
                      sel ? gfx::col::TEXT : gfx::col::TEXT2,
                      sel ? gfx::col::PANEL2 : bg);
            char h[16];
            snprintf(h, sizeof(h), "%08lX", (unsigned long)buttons_[idx].data);
            gfx::textRight(220, y + 17, h, gfx::F_SMALL, gfx::col::DIM,
                           sel ? gfx::col::PANEL2 : bg);
        }
    }

    bool onKey(Key k, KeyEvent e) override {
        switch (k) {
        case Key::Up:
            if (e == KeyEvent::Press || e == KeyEvent::Repeat) {
                if (cursor_ == 0 && device_ > 0) {
                    load(device_ - 1);
                } else if (cursor_ > 0) {
                    cursor_--;
                }
                return true;
            }
            break;
        case Key::Down:
            if (e == KeyEvent::Press || e == KeyEvent::Repeat) {
                if (cursor_ == count_ - 1 && device_ < IrPort::tvCount() - 1) {
                    load(device_ + 1);
                } else if (cursor_ < count_ - 1) {
                    cursor_++;
                }
                return true;
            }
            break;
        case Key::Right:
            if (e == KeyEvent::Press) {
                load((device_ + 1) % IrPort::tvCount());
                return true;
            }
            break;
        case Key::Ok:
            if (e == KeyEvent::Press && buttons_ && cursor_ < count_) {
                buzzer::ok();
                sendForDevice(device_, buttons_[cursor_].data, 32);
                app::toast("sent %s", buttons_[cursor_].name);
                return true;
            }
            break;
        default:
            break;
        }
        return false;
    }

private:
    int              device_ = 0;
    const IrPort::Btn *buttons_ = nullptr;
    int              count_ = 0;
    int              cursor_ = 0, scroll_ = 0;

    void load(int dev) {
        device_ = constrain(dev, 0, IrPort::tvCount() - 1);
        buttons_ = IrPort::tvButtons(device_, count_);
        cursor_ = scroll_ = 0;
        app::invalidate();
    }
};

class IrSaved : public Screen {
public:
    const char *title() const override { return "Коды ИК текущей сессии"; }
    const char *hint() const override { return "< back   OK send   hold OK save all"; }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        if (g_codeCount == 0) {
            gfx::text(12, UI_CONTENT_Y0 + 30, "Коды не приняты", gfx::F_BODY_B,
                      gfx::col::DIM, bg);
            drawWrapped("IR > ИК-приёмник, затем нажмите кнопку.", 12, UI_CONTENT_Y0 + 41,
                        216, 16, gfx::col::DIM, bg, gfx::F_SMALL);
            return;
        }
        const int rowH = 30, visible = 6;
        if (cursor_ < scroll_) scroll_ = cursor_;
        if (cursor_ >= scroll_ + visible) scroll_ = cursor_ - visible + 1;
        for (int i = 0; i < visible; i++) {
            int idx = scroll_ + i;
            if (idx >= g_codeCount) break;
            int y = UI_CONTENT_Y0 + 2 + i * rowH;
            bool sel = idx == cursor_;
            if (sel) gfx::fillRoundRect(4, y, 232, rowH - 2, 5, gfx::col::PANEL2);
            gfx::drawIcon(gfx::Icon::Ir, 12, y + 6, 18,
                          sel ? gfx::col::accent : gfx::col::DIM);
            char b[32];
            snprintf(b, sizeof(b), "0x%08lX", (unsigned long)g_codes[idx].data);
            gfx::text(38, y + 14, b, gfx::F_SMALL, gfx::col::TEXT,
                      sel ? gfx::col::PANEL2 : bg);
            snprintf(b, sizeof(b), "%s  %u bits", irProtoName(g_codes[idx].proto),
                     g_codes[idx].bits);
            gfx::text(38, y + 26, b, gfx::F_SMALL,
                      sel ? gfx::col::TEXT2 : gfx::col::DIM,
                      sel ? gfx::col::PANEL2 : bg);
        }
    }

    bool onKey(Key k, KeyEvent e) override {
        if (g_codeCount == 0) return false;
        if (k == Key::Up && (e == KeyEvent::Press || e == KeyEvent::Repeat)) {
            cursor_ = (cursor_ - 1 + g_codeCount) % g_codeCount;
            return true;
        }
        if (k == Key::Down && (e == KeyEvent::Press || e == KeyEvent::Repeat)) {
            cursor_ = (cursor_ + 1) % g_codeCount;
            return true;
        }
        if (k == Key::Ok) {
            if (e == KeyEvent::Press) {
                ir.sendCode(g_codes[cursor_]);
                app::toast("replayed code");
                return true;
            }
            if (e == KeyEvent::Long) {
                saveAll();
                return true;
            }
        }
        return false;
    }

private:
    int cursor_ = 0, scroll_ = 0;

    void saveAll() {
        if (!storage::mounted()) { app::toast("no microSD"); return; }
        storage::ensureDir(IR_DIR);
        char path[64];
        snprintf(path, sizeof(path), IR_DIR "/session_%lu.txt",
                 (unsigned long)(millis() / 1000));
        String txt = "# IncognitoX IR session\n";
        char line[80];
        for (int i = 0; i < g_codeCount; i++) {
            snprintf(line, sizeof(line), "%s 0x%08lX %u bits\n",
                     irProtoName(g_codes[i].proto),
                     (unsigned long)g_codes[i].data, g_codes[i].bits);
            txt += line;
        }
        storage::writeText(path, txt.c_str());
        app::toast("%d code(s) saved", g_codeCount);
    }
};

}

namespace screens {

void openIr() {
    std::vector<MenuItem> items;
    items.push_back({"ИК-приёмник", gfx::Icon::Ir, "", true,
                     [] { app::push(new IrReceive()); }});
    items.push_back({"Универсальный пульт", gfx::Icon::Play, "", true,
                     [] { app::push(new IrRemote()); }});
    items.push_back({"Коды сессии", gfx::Icon::Log, "", true,
                     [] { app::push(new IrSaved()); }});
    items.push_back({"ИК-глушилка", gfx::Icon::Lock, "", false,
                     [] { app::push(new LockedScreen(
                              "IR jammer",
                              "Holding a 38 kHz carrier blocks everyone's remote in "
                              "the room and is only useful for sabotage. Not "
                              "implemented.",
                              "Decode your own remotes and use the universal remote to "
                              "send individual commands.")); }});
    app::push(new ListScreen("Infrared", std::move(items),
                             "< back   OK open   ^v move"));
}

}
