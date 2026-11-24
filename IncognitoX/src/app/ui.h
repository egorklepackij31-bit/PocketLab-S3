#pragma once

#include <functional>
#include <string>
#include <vector>

#include "app/app.h"
#include "hw/gfx.h"

struct MenuItem {
    String          label;
    gfx::Icon       icon = gfx::Icon::None;
    String          value;
    bool            enabled = true;
    std::function<void()> action;
};

class ListScreen : public Screen {
public:
    ListScreen(const char *title, std::vector<MenuItem> items,
               const char *hint = nullptr);

    const char *title() const override { return title_; }
    void draw() override;
    bool onKey(Key k, KeyEvent e) override;
    const char *hint() const override { return hint_; }

    void setItems(std::vector<MenuItem> items);
    int  cursor() const { return cursor_; }
    void setCursor(int c);

protected:
    const char *title_;
    const char *hint_;
    std::vector<MenuItem> items_;
    int cursor_ = 0;
    int scroll_ = 0;

private:
    void clampScroll();
};

class TextScreen : public Screen {
public:
    TextScreen(const char *title, const char *hint = nullptr);
    void setText(const std::string &text);
    void addLine(const char *line);
    void clear();

    const char *title() const override { return title_; }
    void draw() override;
    bool onKey(Key k, KeyEvent e) override;
    const char *hint() const override { return hint_; }

    int  lines() const { return (int)lines_.size(); }
    void scrollToEnd();

private:
    const char *title_;
    const char *hint_;
    std::vector<String> lines_;
    int top_ = 0;
};

class LockedScreen : public Screen {
public:
    LockedScreen(const char *title, const char *why, const char *instead);
    const char *title() const override { return title_; }
    void draw() override;
    bool onKey(Key k, KeyEvent e) override { (void)k; (void)e; return false; }
    const char *hint() const override { return "< back"; }

private:
    const char *title_;
    const char *why_;
    const char *instead_;
};

int drawWrapped(const char *text, int x, int y, int w, int lineH, uint16_t fg,
                uint16_t bg, const GFXfont *f);
