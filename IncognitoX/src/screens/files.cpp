#include <vector>

#include "app/app.h"
#include "app/ui.h"
#include "config.h"
#include "hw/buzzer.h"
#include "hw/gfx.h"
#include "hw/input.h"
#include "hw/storage.h"
#include "screens/screens.h"

namespace {

String humanSize(size_t n) {
    char b[24];
    if (n < 1024) snprintf(b, sizeof(b), "%u B", (unsigned)n);
    else if (n < 1024 * 1024) snprintf(b, sizeof(b), "%.1f KB", n / 1024.0);
    else snprintf(b, sizeof(b), "%.1f MB", n / (1024.0 * 1024.0));
    return String(b);
}

bool isTextFile(const String &name) {
    String n = name;
    n.toLowerCase();
    return n.endsWith(".txt") || n.endsWith(".log") || n.endsWith(".csv") ||
           n.endsWith(".ini") || n.endsWith(".json") || n.endsWith(".md") ||
           n.endsWith(".h") || n.endsWith(".c") || n.endsWith(".cpp");
}

class FileBrowser : public Screen {
public:
    const char *title() const override {
        snprintf(titleBuf_, sizeof(titleBuf_), "%s", path_.c_str());
        return titleBuf_;
    }
    const char *hint() const override {
        return "< вверх   OK открыть   удержание OK удалить   > обновить";
    }

    void enter() override { reload(); }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        if (!storage::mounted()) {
            gfx::text(12, UI_CONTENT_Y0 + 30, "Нет карты microSD", gfx::F_BODY_B,
                      gfx::col::WARN, bg);
            gfx::text(12, UI_CONTENT_Y0 + 54, "Вставьте карту FAT32", gfx::F_SMALL,
                      gfx::col::DIM, bg);
            return;
        }
        if (entries_.empty()) {
            gfx::text(12, UI_CONTENT_Y0 + 30, "Пустая папка", gfx::F_BODY_B,
                      gfx::col::DIM, bg);
            return;
        }

        const int rowH = 26;
        int visible = (UI_CONTENT_Y1 - UI_CONTENT_Y0 - 4) / rowH;
        if (cursor_ < scroll_) scroll_ = cursor_;
        if (cursor_ >= scroll_ + visible) scroll_ = cursor_ - visible + 1;

        for (int i = 0; i < visible; i++) {
            int idx = scroll_ + i;
            if (idx >= (int)entries_.size()) break;
            const auto &e = entries_[idx];
            int y = UI_CONTENT_Y0 + 2 + i * rowH;
            bool sel = idx == cursor_;
            if (sel) gfx::fillRoundRect(4, y, 232, rowH - 2, 5, gfx::col::PANEL2);
            gfx::drawIcon(e.dir ? gfx::Icon::Folder : gfx::Icon::Log, 12, y + 4, 18,
                          sel ? gfx::col::accent : gfx::col::DIM);
            char nm[40];
            gfx::ellipsize(nm, sizeof(nm), e.name.c_str(), 180, gfx::F_SMALL);
            gfx::text(38, y + 17, nm, gfx::F_SMALL, gfx::col::TEXT,
                      sel ? gfx::col::PANEL2 : bg);
            if (!e.dir) {
                gfx::textRight(220, y + 17, humanSize(e.size).c_str(),
                               gfx::F_SMALL, gfx::col::DIM,
                               sel ? gfx::col::PANEL2 : bg);
            }
        }
    }

    bool onKey(Key k, KeyEvent e) override {
        if (entries_.empty()) {
            if (k == Key::Right) { reload(); return true; }
            return false;
        }
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
        case Key::Right:
            if (e == KeyEvent::Press) { reload(); return true; }
            break;
        case Key::Left:
            if (e == KeyEvent::Press || e == KeyEvent::Repeat) {
                if (path_ != "/" && path_.length() > 1) {
                    int p = path_.lastIndexOf('/');
                    path_ = p > 0 ? path_.substring(0, p) : "/";
                    cursor_ = scroll_ = 0;
                    reload();
                    return true;
                }
            }
            return false;
        case Key::Ok:
            if (e == KeyEvent::Long) {
                deleteCurrent();
                return true;
            }
            if (e == KeyEvent::Press) {
                activate();
                return true;
            }
            break;
        default:
            break;
        }
        return false;
    }

private:
    String                      path_ = "/";
    std::vector<storage::Entry> entries_;
    int                         cursor_ = 0, scroll_ = 0;
    mutable char                titleBuf_[32] = "/";

    String fullPath(const storage::Entry &e) const {
        if (path_ == "/") return String("/") + e.name;
        return path_ + "/" + e.name;
    }

    String parentOf(const String &p) const {
        int i = p.lastIndexOf('/');
        return i > 0 ? p.substring(0, i) : String("/");
    }

    void reload() {
        entries_.clear();
        storage::list(path_.c_str(), entries_);
        if (cursor_ >= (int)entries_.size())
            cursor_ = max(0, (int)entries_.size() - 1);
        clampScroll();
        app::invalidate();
    }

    void clampScroll() {
        int rowH = 26;
        int visible = (UI_CONTENT_Y1 - UI_CONTENT_Y0 - 4) / rowH;
        if (cursor_ < scroll_) scroll_ = cursor_;
        if (cursor_ >= scroll_ + visible) scroll_ = cursor_ - visible + 1;
        if (scroll_ < 0) scroll_ = 0;
        if (scroll_ > (int)entries_.size() - visible)
            scroll_ = max(0, (int)entries_.size() - visible);
    }

    void activate() {
        const storage::Entry e = entries_[cursor_];
        if (e.dir) {
            path_ = fullPath(e);
            cursor_ = scroll_ = 0;
            reload();
            return;
        }
        String p = fullPath(e);
        if (isTextFile(e.name)) {
            String txt = storage::readText(p.c_str(), 12000);
            TextScreen *ts = new TextScreen("Просмотр файла");
            app::push(ts);
            ts->setText(txt.length() ? txt.c_str() : "(empty file)");
            return;
        }
        String msg = e.name + "  " + humanSize(e.size) +
                     ". Use the viewer for .txt, .log and .csv files.";
        app::message("Файл", msg.c_str());
    }

    void deleteCurrent() {
        const storage::Entry e = entries_[cursor_];
        String p = fullPath(e);
        if (app::confirm("Удалить?", p.c_str())) {
            bool ok = false;
            if (e.dir) {

                std::vector<storage::Entry> probe;
                if (storage::list(p.c_str(), probe) && probe.empty()) {
                    if (storage::fs()->rmdir(p.c_str())) ok = true;
                } else {
                    app::message("Папка не пуста",
                                 "Сначала удалите файлы внутри или используйте кардридер.");
                    return;
                }
            } else {
                ok = storage::remove(p.c_str());
            }
            if (ok) {
                buzzer::ok();
                app::toast("удалено");
                reload();
            } else {
                buzzer::err();
                app::toast("ошибка удаления");
            }
        }
    }
};

}

namespace screens {

void openFiles() {
    app::push(new FileBrowser());
}

}
