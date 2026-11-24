#include <vector>

#include "app/app.h"
#include "app/ui.h"
#include "hw/buzzer.h"
#include "hw/gfx.h"
#include "hw/input.h"
#include "screens/screens.h"

namespace {

constexpr int SN_COLS = 24;
constexpr int SN_ROWS = 12;
constexpr int SN_CELL = 10;
constexpr int SN_BOARD_Y = 36;

class SnakeGame : public Screen {
public:
    const char *title() const override { return "Змейка"; }
    const char *hint() const override {
        return "< выход   ^v<> поворот   OK пауза / заново";
    }

    void enter() override {
        restart();
    }

    void tick() override {
        if (paused_ || dead_) return;
        if (millis() - lastStep_ < stepMs_) return;
        lastStep_ = millis();
        step();
        app::invalidate();
    }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        char b[48];
        snprintf(b, sizeof(b), "счёт %d   рекорд %d   длина %d", score_, best_, len_);
        gfx::text(10, 28, b, gfx::F_SMALL, gfx::col::TEXT2, bg);
        if (paused_)
            gfx::textRight(220, 28, "ПАУЗА", gfx::F_SMALL, gfx::col::WARN, bg);

        gfx::fillRect(0, SN_BOARD_Y, 240, SN_ROWS * SN_CELL, gfx::col::BG);
        gfx::hline(0, SN_BOARD_Y - 1, 240, gfx::col::accent);
        gfx::hline(0, SN_BOARD_Y + SN_ROWS * SN_CELL, 240, gfx::col::PANEL3);
        gfx::vline(0, SN_BOARD_Y, SN_ROWS * SN_CELL, gfx::col::PANEL3);
        gfx::vline(239, SN_BOARD_Y, SN_ROWS * SN_CELL, gfx::col::PANEL3);

        for (int i = 0; i < len_; i++) {
            int x = bx_[i] * SN_CELL + 1;
            int y = SN_BOARD_Y + by_[i] * SN_CELL + 1;
            uint16_t c = (i == 0) ? gfx::col::ORANGE
                                  : (i & 1) ? gfx::col::accent
                                            : gfx::col::accentDim();
            gfx::fillRect(x, y, SN_CELL - 2, SN_CELL - 2, c);
            if (i == 0) {
                gfx::fillRect(bx_[0] * SN_CELL + 4, SN_BOARD_Y + by_[0] * SN_CELL + 3,
                              2, 2, gfx::col::BLACK);
            }
        }
        gfx::fillCircle(foodX_ * SN_CELL + SN_CELL / 2,
                        SN_BOARD_Y + foodY_ * SN_CELL + SN_CELL / 2, 3,
                        gfx::col::ERR);

        if (dead_) {
            gfx::fillRoundRect(50, 92, 140, 72, 10, gfx::col::PANEL);
            gfx::roundRect(50, 92, 140, 72, 10, gfx::col::ERR);
            gfx::textCenter(120, 120, "ИГРА ОКОНЧЕНА", gfx::F_BODY_B, gfx::col::ERR,
                            gfx::col::PANEL);
            snprintf(b, sizeof(b), "счёт %d", score_);
            gfx::textCenter(120, 142, b, gfx::F_SMALL, gfx::col::TEXT2,
                            gfx::col::PANEL);
            gfx::textCenter(120, 156, "OK = заново", gfx::F_SMALL, gfx::col::DIM,
                            gfx::col::PANEL);
        }
    }

    bool onKey(Key k, KeyEvent e) override {
        if (k == Key::Ok) {
            if (e == KeyEvent::Long) {
                app::pop();
                return true;
            }
            if (e == KeyEvent::Press) {
                if (dead_) restart();
                else paused_ = !paused_;
                app::invalidate();
                return true;
            }
        }
        if (k == Key::Up && (e == KeyEvent::Press || e == KeyEvent::Repeat)) {
            if (dirY_ == 0) nextDirX_ = 0, nextDirY_ = -1;
            return true;
        }
        if (k == Key::Down && (e == KeyEvent::Press || e == KeyEvent::Repeat)) {
            if (dirY_ == 0) nextDirX_ = 0, nextDirY_ = 1;
            return true;
        }
        if (k == Key::Left && (e == KeyEvent::Press || e == KeyEvent::Repeat)) {
            if (dirX_ == 0) nextDirX_ = -1, nextDirY_ = 0;
            return true;
        }
        if (k == Key::Right && (e == KeyEvent::Press || e == KeyEvent::Repeat)) {
            if (dirX_ == 0) nextDirX_ = 1, nextDirY_ = 0;
            return true;
        }
        return false;
    }

private:
    uint8_t  bx_[SN_COLS * SN_ROWS] = {0};
    uint8_t  by_[SN_COLS * SN_ROWS] = {0};
    int      len_ = 3;
    int      dirX_ = 1, dirY_ = 0;
    int      nextDirX_ = 1, nextDirY_ = 0;
    int      foodX_ = 10, foodY_ = 8;
    int      score_ = 0;
    int      best_ = 0;
    bool     dead_ = false;
    bool     paused_ = false;
    uint32_t lastStep_ = 0;
    uint32_t stepMs_ = 140;

    void restart() {
        len_ = 3;
        bx_[0] = 8; by_[0] = 8;
        bx_[1] = 7; by_[1] = 8;
        bx_[2] = 6; by_[2] = 8;
        dirX_ = nextDirX_ = 1;
        dirY_ = nextDirY_ = 0;
        score_ = 0;
        dead_ = false;
        paused_ = false;
        stepMs_ = 140;
        lastStep_ = millis();
        placeFood();
        app::invalidate();
    }

    void placeFood() {
        for (int tries = 0; tries < 2000; tries++) {
            int x = (int)random(SN_COLS);
            int y = (int)random(SN_ROWS);
            bool hit = false;
            for (int i = 0; i < len_; i++)
                if (bx_[i] == x && by_[i] == y) { hit = true; break; }
            if (!hit) { foodX_ = x; foodY_ = y; return; }
        }
    }

    void step() {
        if (nextDirX_ || nextDirY_) {
            dirX_ = nextDirX_;
            dirY_ = nextDirY_;
        }
        int nx = bx_[0] + dirX_;
        int ny = by_[0] + dirY_;
        if (nx < 0 || nx >= SN_COLS || ny < 0 || ny >= SN_ROWS) return die();
        for (int i = 0; i < len_ - 1; i++)
            if (bx_[i] == nx && by_[i] == ny) return die();

        bool eat = (nx == foodX_ && ny == foodY_);
        if (!eat) {

            for (int i = len_ - 1; i > 0; i--) {
                bx_[i] = bx_[i - 1];
                by_[i] = by_[i - 1];
            }
        } else {
            for (int i = len_; i > 0; i--) {
                bx_[i] = bx_[i - 1];
                by_[i] = by_[i - 1];
            }
            if (len_ < SN_COLS * SN_ROWS - 1) len_++;
            score_ += 10;
            if (score_ > best_) best_ = score_;
            if (stepMs_ > 70 && score_ % 50 == 0) stepMs_ -= 10;
            buzzer::beep(2400, 15);
            placeFood();
        }
        bx_[0] = (uint8_t)nx;
        by_[0] = (uint8_t)ny;
    }

    void die() {
        dead_ = true;
        buzzer::play(buzzer::MEL_ERROR);
        if (score_ > best_) best_ = score_;
    }
};

constexpr int T_COLS = 10;
constexpr int T_ROWS = 20;
constexpr int T_CELL = 9;
constexpr int T_X = 75;
constexpr int T_Y = 34;

struct Tetromino {
    int8_t  cells[4][4];
    uint8_t color;
};

uint16_t tetColor(uint8_t idx) {
    switch (idx) {
    case 1: return gfx::col::BLUE;
    case 2: return gfx::col::WARN;
    case 3: return gfx::col::VIOLET;
    case 4: return gfx::col::OK;
    case 5: return gfx::col::ERR;
    case 6: return gfx::col::ORANGE;
    default: return gfx::col::accent;
    }
}

class TetrisGame : public Screen {
public:
    const char *title() const override { return "Тетрис"; }
    const char *hint() const override {
        return "<> движение   ^ поворот   v вниз   удержание OK выход";
    }

    void enter() override {
        memset(board_, 0, sizeof(board_));
        score_ = 0;
        lines_ = 0;
        level_ = 1;
        dead_ = false;
        paused_ = false;
        nextType_ = (int)random(7);
        spawn();
        lastDrop_ = millis();
    }

    void tick() override {
        if (dead_ || paused_) return;
        uint32_t interval = max(110, 620 - (level_ - 1) * 55);
        if (millis() - lastDrop_ < interval) return;
        lastDrop_ = millis();
        if (!move(0, 1)) lockPiece();
        app::invalidate();
    }

    void draw() override {
        const uint16_t bg = gfx::col::BG;

        gfx::text(10, 27, "ТЕТРИС", gfx::F_SMALL_B, gfx::col::accent, bg);

        gfx::fillRect(T_X - 2, T_Y - 2, T_COLS * T_CELL + 4, T_ROWS * T_CELL + 4,
                      gfx::col::PANEL);
        for (int y = 0; y < T_ROWS; y++)
            for (int x = 0; x < T_COLS; x++) {
                int px = T_X + x * T_CELL;
                int py = T_Y + y * T_CELL;
                if (board_[y][x])
                    gfx::fillRect(px, py, T_CELL - 1, T_CELL - 1,
                                  tetColor(board_[y][x]));
                else
                    gfx::fillRect(px, py, T_CELL - 1, T_CELL - 1,
                                  (x + y) & 1 ? gfx::col::BG : gfx::col::BG2);
            }

        if (!dead_) {
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 4; x++)
                    if (piece_.cells[y][x]) {
                        int px = T_X + (pieceX_ + x) * T_CELL;
                        int py = T_Y + (pieceY_ + y) * T_CELL;
                        gfx::fillRect(px, py, T_CELL - 1, T_CELL - 1,
                                      tetColor(piece_.color));
                    }
        }

        int px = T_X + T_COLS * T_CELL + 14;
        char b[32];
        gfx::text(px, 60, "СЧЁТ", gfx::F_SMALL, gfx::col::DIM, bg);
        snprintf(b, sizeof(b), "%d", score_);
        gfx::text(px, 84, b, gfx::F_BODY_B, gfx::col::TEXT, bg);
        gfx::text(px, 108, "ЛИНИИ", gfx::F_SMALL, gfx::col::DIM, bg);
        snprintf(b, sizeof(b), "%d", lines_);
        gfx::text(px, 128, b, gfx::F_BODY, gfx::col::TEXT2, bg);
        gfx::text(px, 152, "УРОВЕНЬ", gfx::F_SMALL, gfx::col::DIM, bg);
        snprintf(b, sizeof(b), "%d", level_);
        gfx::text(px, 172, b, gfx::F_BODY, gfx::col::TEXT2, bg);

        gfx::text(px, 196, "ДАЛЕЕ", gfx::F_SMALL, gfx::col::DIM, bg);
        Tetromino n = makePiece(nextType_);
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
                if (n.cells[y][x])
                    gfx::fillRect(px + x * 8, 204 + y * 8, 7, 7,
                                  tetColor(n.color));

        if (dead_) {
            gfx::fillRoundRect(16, 86, 208, 72, 10, gfx::col::PANEL);
            gfx::roundRect(16, 86, 208, 72, 10, gfx::col::ERR);
            gfx::textCenter(120, 114, "ИГРА ОКОНЧЕНА", gfx::F_BODY_B, gfx::col::ERR,
                            gfx::col::PANEL);
            snprintf(b, sizeof(b), "счёт %d", score_);
            gfx::textCenter(120, 136, b, gfx::F_SMALL, gfx::col::TEXT2,
                            gfx::col::PANEL);
            gfx::textCenter(120, 150, "OK = заново", gfx::F_SMALL, gfx::col::DIM,
                            gfx::col::PANEL);
        } else if (paused_) {
            gfx::fillRoundRect(90, 100, 140, 44, 8, gfx::col::PANEL);
            gfx::textCenter(120, 128, "ПАУЗА", gfx::F_BODY_B, gfx::col::WARN,
                            gfx::col::PANEL);
        }
    }

    bool onKey(Key k, KeyEvent e) override {
        if (k == Key::Ok) {
            if (e == KeyEvent::Long) {
                app::pop();
                return true;
            }
            if (e == KeyEvent::Press) {
                if (dead_) {
                    enter();
                } else {

                    while (move(0, 1)) {}
                    lockPiece();
                }
                app::invalidate();
                return true;
            }
        }
        if (dead_ || paused_) return true;
        if (k == Key::Left && (e == KeyEvent::Press || e == KeyEvent::Repeat)) {
            move(-1, 0);
            return true;
        }
        if (k == Key::Right && (e == KeyEvent::Press || e == KeyEvent::Repeat)) {
            move(1, 0);
            return true;
        }
        if (k == Key::Down && (e == KeyEvent::Press || e == KeyEvent::Repeat)) {
            if (!move(0, 1)) lockPiece();
            return true;
        }
        if (k == Key::Up && e == KeyEvent::Press) {
            rotate();
            return true;
        }
        return false;
    }

private:
    uint8_t    board_[T_ROWS][T_COLS] = {{0}};
    Tetromino  piece_;
    int        pieceX_ = 3, pieceY_ = 0;
    int        nextType_ = 0;
    int        score_ = 0, lines_ = 0, level_ = 1;
    bool       dead_ = false, paused_ = false;
    uint32_t   lastDrop_ = 0;

    static Tetromino makePiece(int type) {
        Tetromino t;
        memset(&t, 0, sizeof(t));
        t.color = (uint8_t)(type + 1);
        switch (type) {
        case 0:
            t.cells[1][0] = t.cells[1][1] = t.cells[1][2] = t.cells[1][3] = 1;
            break;
        case 1:
            t.cells[1][1] = t.cells[1][2] = t.cells[2][1] = t.cells[2][2] = 1;
            break;
        case 2:
            t.cells[1][1] = t.cells[2][0] = t.cells[2][1] = t.cells[2][2] = 1;
            break;
        case 3:
            t.cells[1][1] = t.cells[1][2] = t.cells[2][0] = t.cells[2][1] = 1;
            break;
        case 4:
            t.cells[1][0] = t.cells[1][1] = t.cells[2][1] = t.cells[2][2] = 1;
            break;
        case 5:
            t.cells[1][0] = t.cells[2][0] = t.cells[2][1] = t.cells[2][2] = 1;
            break;
        default:
            t.cells[1][2] = t.cells[2][0] = t.cells[2][1] = t.cells[2][2] = 1;
            break;
        }
        return t;
    }

    void spawn() {
        piece_ = makePiece(nextType_);
        nextType_ = (int)random(7);
        pieceX_ = 3;
        pieceY_ = 0;
        if (collides(pieceX_, pieceY_, piece_)) {
            dead_ = true;
            buzzer::play(buzzer::MEL_ERROR);
        }
    }

    bool collides(int px, int py, const Tetromino &t) const {
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++) {
                if (!t.cells[y][x]) continue;
                int nx = px + x, ny = py + y;
                if (nx < 0 || nx >= T_COLS || ny >= T_ROWS) return true;
                if (ny >= 0 && board_[ny][nx]) return true;
            }
        return false;
    }

    bool move(int dx, int dy) {
        if (collides(pieceX_ + dx, pieceY_ + dy, piece_)) return false;
        pieceX_ += dx;
        pieceY_ += dy;
        return true;
    }

    void rotate() {
        Tetromino r = piece_;
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++) r.cells[y][x] = piece_.cells[3 - x][y];

        for (int kick = 0; kick <= 2; kick++) {
            if (!collides(pieceX_ + kick, pieceY_, r)) {
                piece_ = r;
                pieceX_ += kick;
                return;
            }
            if (kick > 0 && !collides(pieceX_ - kick, pieceY_, r)) {
                piece_ = r;
                pieceX_ -= kick;
                return;
            }
        }
    }

    void lockPiece() {
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
                if (piece_.cells[y][x]) {
                    int nx = pieceX_ + x, ny = pieceY_ + y;
                    if (ny >= 0 && ny < T_ROWS && nx >= 0 && nx < T_COLS)
                        board_[ny][nx] = piece_.color;
                }
        int cleared = 0;
        for (int y = T_ROWS - 1; y >= 0; y--) {
            bool full = true;
            for (int x = 0; x < T_COLS; x++)
                if (!board_[y][x]) { full = false; break; }
            if (!full) continue;
            cleared++;
            for (int yy = y; yy > 0; yy--)
                memcpy(board_[yy], board_[yy - 1], T_COLS);
            memset(board_[0], 0, T_COLS);
            y++;
        }
        if (cleared) {
            lines_ += cleared;
            score_ += cleared * cleared * 100;
            level_ = 1 + lines_ / 10;
            buzzer::beep(2000 + cleared * 300, 40);
        }
        spawn();
        lastDrop_ = millis();
    }
};

}

namespace screens {

void openGames() {
    std::vector<MenuItem> items;
    items.push_back({"Змейка", gfx::Icon::Game, "", true,
                     [] { app::push(new SnakeGame()); }});
    items.push_back({"Тетрис", gfx::Icon::Game, "", true,
                     [] { app::push(new TetrisGame()); }});
    items.push_back({"Управление", gfx::Icon::Info, "", true, [] {
                         TextScreen *t = new TextScreen("Управление");
                         app::push(t);
                         t->setText(
                             "Snake\n"
                             "  arrows  turn\n"
                             "  OK      pause / restart\n"
                             "  hold OK exit\n\n"
                             "Tetris\n"
                             "  left/right move\n"
                             "  up         rotate\n"
                             "  down       soft drop\n"
                             "  OK         hard drop\n"
                             "  hold OK    exit\n\n"
                             "Left is also the global Back key, so\n"
                             "в играх удерживайте OK для выхода.");
                     }});
    app::push(new ListScreen("Games", std::move(items), "< back   OK start"));
}

}
