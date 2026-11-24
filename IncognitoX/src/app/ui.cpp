#include "app/ui.h"

#include "app/settings.h"
#include "config.h"
#include "hw/buzzer.h"

namespace {
constexpr int ROW = UI_ROW_H;
constexpr int TOP = UI_CONTENT_Y0 + 3;
constexpr int BOTTOM = UI_CONTENT_Y1 - 2;
}

int drawWrapped(const char *text, int x, int y, int w, int lineH, uint16_t fg,
                uint16_t bg, const GFXfont *f) {
    if (!text) return y;
    char buf[256];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    int cy = y;
    char line[96] = {0};
    char *p = buf;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        char word[48] = {0};
        int i = 0;
        while (*p && *p != ' ' && *p != '\n' && i < 47) word[i++] = *p++;
        word[i] = 0;
        bool nl = (*p == '\n');
        if (nl) p++;

        char probe[160];
        snprintf(probe, sizeof(probe), "%s%s%s", line, line[0] ? " " : "", word);
        if (gfx::textWidth(probe, f) > w && line[0]) {
            gfx::text(x, cy + gfx::ascent(f), line, f, fg, bg);
            cy += lineH;
            strncpy(line, word, sizeof(line) - 1);
        } else {
            strncpy(line, probe, sizeof(line) - 1);
        }
        if (nl) {
            gfx::text(x, cy + gfx::ascent(f), line, f, fg, bg);
            cy += lineH;
            line[0] = 0;
        }
    }
    if (line[0]) {
        gfx::text(x, cy + gfx::ascent(f), line, f, fg, bg);
        cy += lineH;
    }
    return cy;
}

ListScreen::ListScreen(const char *title, std::vector<MenuItem> items,
                       const char *hint)
    : title_(title), hint_(hint ? hint : "< back   OK open"), items_(std::move(items)) {}

void ListScreen::setItems(std::vector<MenuItem> items) {
    items_ = std::move(items);
    if (cursor_ >= (int)items_.size()) cursor_ = (int)items_.size() - 1;
    if (cursor_ < 0) cursor_ = 0;
    clampScroll();
    app::invalidate();
}

void ListScreen::setCursor(int c) {
    cursor_ = c;
    if (cursor_ >= (int)items_.size()) cursor_ = (int)items_.size() - 1;
    if (cursor_ < 0) cursor_ = 0;
    clampScroll();
}

void ListScreen::clampScroll() {
    const int visible = (BOTTOM - TOP) / ROW;
    if (cursor_ < scroll_) scroll_ = cursor_;
    if (cursor_ >= scroll_ + visible) scroll_ = cursor_ - visible + 1;
    if (scroll_ < 0) scroll_ = 0;
    if (scroll_ > (int)items_.size() - visible) {
        scroll_ = max(0, (int)items_.size() - visible);
    }
}

void ListScreen::draw() {
    const uint16_t bg = gfx::col::BG;
    const int visible = (BOTTOM - TOP) / ROW;
    clampScroll();

    for (int i = 0; i < visible; i++) {
        int idx = scroll_ + i;
        if (idx >= (int)items_.size()) break;
        const MenuItem &it = items_[idx];
        int y = TOP + i * ROW;
        bool sel = (idx == cursor_);

        if (sel) {
            gfx::fillRoundRect(4, y, 232, ROW - 3, 6,
                               it.enabled ? gfx::col::accent : gfx::col::PANEL3);
        }
        uint16_t fg = sel ? gfx::col::BLACK
                          : (it.enabled ? gfx::col::TEXT : gfx::col::DIM2);
        uint16_t iconCol = sel ? gfx::col::BLACK
                               : (it.enabled ? gfx::col::accent : gfx::col::DIM2);
        int iconX = 12;
        if (it.icon != gfx::Icon::None) {
            gfx::drawIcon(it.icon, iconX, y + 4, 20, iconCol);
        } else {
            gfx::fillCircle(22, y + (ROW - 3) / 2, 3, iconCol);
        }

        char lab[40];
        char val[36];
        int vw = 0;
        bool hasValue = it.value.length() > 0;
        if (hasValue) {
            gfx::ellipsize(val, sizeof(val), it.value.c_str(), 64, gfx::F_SMALL);
            vw = gfx::textWidth(val, gfx::F_SMALL);
        }
        int maxW = hasValue ? (200 - 40 - vw - 6) : 166;
        if (maxW < 40) maxW = 40;
        gfx::ellipsize(lab, sizeof(lab), it.label.c_str(), maxW, gfx::F_SMALL);
        gfx::text(40, y + 19, lab, gfx::F_SMALL, fg,
                  sel ? (it.enabled ? gfx::col::accent : gfx::col::PANEL3) : bg);

        if (hasValue) {
            gfx::textRight(200, y + 19, val, gfx::F_SMALL,
                           sel ? gfx::col::BLACK : gfx::col::DIM,
                           sel ? (it.enabled ? gfx::col::accent : gfx::col::PANEL3)
                               : bg);
        }
        if (sel && it.enabled && !hasValue) {
            gfx::drawIcon(gfx::Icon::Play, 212, y + 8, 12, gfx::col::BLACK);
        }
    }

    int total = (int)items_.size();
    if (total > visible) {
        int trackH = BOTTOM - TOP;
        int barH = max(12, trackH * visible / total);
        int barY = TOP + (trackH - barH) * cursor_ / max(1, total - 1);
        gfx::fillRoundRect(232, TOP, 3, trackH, 1, gfx::col::PANEL2);
        gfx::fillRoundRect(232, barY, 3, barH, 1, gfx::col::accent);
    }
}

bool ListScreen::onKey(Key k, KeyEvent e) {
    if (items_.empty()) return false;
    const int visible = (BOTTOM - TOP) / ROW;
    int total = (int)items_.size();

    switch (k) {
    case Key::Up:
        if (e == KeyEvent::Press || e == KeyEvent::Repeat) {
            cursor_ = (cursor_ - 1 + total) % total;
            clampScroll();
            return true;
        }
        break;
    case Key::Down:
        if (e == KeyEvent::Press || e == KeyEvent::Repeat) {
            cursor_ = (cursor_ + 1) % total;
            clampScroll();
            return true;
        }
        break;
    case Key::Right:
        if (e == KeyEvent::Press) {
            cursor_ = min(total - 1, cursor_ + visible);
            clampScroll();
            return true;
        }
        break;
    case Key::Ok:
        if (e == KeyEvent::Press) {
            MenuItem &it = items_[cursor_];
            if (!it.enabled) { buzzer::err(); return true; }
            buzzer::ok();
            if (it.action) it.action();
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

TextScreen::TextScreen(const char *title, const char *hint)
    : title_(title), hint_(hint ? hint : "< back   ^v scroll") {}

void TextScreen::setText(const std::string &text) {
    lines_.clear();
    String cur;
    for (char c : text) {
        if (c == '\n') {
            lines_.push_back(cur);
            cur = "";
        } else if (c != '\r') {
            cur += c;
        }
    }
    if (cur.length()) lines_.push_back(cur);
    top_ = 0;
}

void TextScreen::addLine(const char *line) {
    lines_.push_back(String(line ? line : ""));
    scrollToEnd();
}

void TextScreen::clear() {
    lines_.clear();
    top_ = 0;
}

void TextScreen::scrollToEnd() {
    const int linesPerPage = (UI_CONTENT_Y1 - UI_CONTENT_Y0 - 6) / 11;
    top_ = max(0, (int)lines_.size() - linesPerPage);
}

void TextScreen::draw() {
    const uint16_t bg = gfx::col::BG;
    const int lh = 11;
    const int linesPerPage = (UI_CONTENT_Y1 - UI_CONTENT_Y0 - 6) / lh;
    for (int i = 0; i < linesPerPage; i++) {
        int idx = top_ + i;
        if (idx >= (int)lines_.size()) break;
        gfx::text5(6, UI_CONTENT_Y0 + 4 + i * lh, lines_[idx].c_str(),
                   gfx::col::TEXT2, bg, 1);
    }
    int total = (int)lines_.size();
    if (total > linesPerPage) {
        int trackH = UI_CONTENT_Y1 - UI_CONTENT_Y0 - 6;
        int barH = max(10, trackH * linesPerPage / total);
        int barY = UI_CONTENT_Y0 + 3 + (trackH - barH) * top_ / max(1, total - linesPerPage);
        gfx::fillRoundRect(232, UI_CONTENT_Y0 + 3, 3, barH, 1, gfx::col::accent);
    }
}

bool TextScreen::onKey(Key k, KeyEvent e) {
    if (e != KeyEvent::Press && e != KeyEvent::Repeat) return false;
    const int lh = 11;
    const int linesPerPage = (UI_CONTENT_Y1 - UI_CONTENT_Y0 - 6) / lh;
    int maxTop = max(0, (int)lines_.size() - linesPerPage);
    if (k == Key::Up) { top_ = max(0, top_ - 1); return true; }
    if (k == Key::Down) { top_ = min(maxTop, top_ + 1); return true; }
    if (k == Key::Right) { top_ = maxTop; return true; }
    return false;
}

LockedScreen::LockedScreen(const char *title, const char *why, const char *instead)
    : title_(title), why_(why), instead_(instead) {}

void LockedScreen::draw() {
    const uint16_t bg = gfx::col::BG;
    gfx::fillRoundRect(12, UI_CONTENT_Y0 + 8, 200, 48, 8, gfx::col::PANEL);
    gfx::drawIcon(gfx::Icon::Lock, 22, UI_CONTENT_Y0 + 17, 28, gfx::col::accent);
    gfx::text(60, UI_CONTENT_Y0 + 40, "Отключено", gfx::F_BODY_B,
              gfx::col::TEXT, gfx::col::PANEL);

    int y = UI_CONTENT_Y0 + 70;
    y = drawWrapped(why_, 14, y, 180, 18, gfx::col::TEXT2, bg, gfx::F_SMALL) + 6;
    if (instead_ && *instead_) {
        gfx::hline(14, y - 2, 180, gfx::col::LINE);
        y += 8;
        gfx::text(14, y + 12, "Что можно вместо этого:", gfx::F_SMALL_B,
                  gfx::col::accent, bg);
        y += 20;
        drawWrapped(instead_, 14, y, 180, 18, gfx::col::TEXT2, bg, gfx::F_SMALL);
    }
}
