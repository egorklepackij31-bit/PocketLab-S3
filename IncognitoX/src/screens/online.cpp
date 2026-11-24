#include <math.h>
#include <vector>

#include "app/app.h"
#include "app/log.h"
#include "app/ui.h"
#include "config.h"
#include "hw/buzzer.h"
#include "hw/display.h"
#include "hw/gfx.h"
#include "hw/gps.h"
#include "hw/input.h"
#include "lib/net.h"
#include "screens/screens.h"

namespace {

constexpr double kMinskLat = 53.9023;
constexpr double kMinskLon = 27.5619;
constexpr double kEarthKm  = 6371.0;
constexpr double kRad      = 0.017453292519943295;
constexpr double kDeg      = 57.29577951308232;

double distanceKm(double lat1, double lon1, double lat2, double lon2) {
    double dLat = (lat2 - lat1) * kRad;
    double dLon = (lon2 - lon1) * kRad;
    double a = sin(dLat / 2) * sin(dLat / 2) +
               cos(lat1 * kRad) * cos(lat2 * kRad) * sin(dLon / 2) * sin(dLon / 2);
    if (a > 1) a = 1;
    return 2 * kEarthKm * asin(sqrt(a));
}

double bearingDeg(double lat1, double lon1, double lat2, double lon2) {
    double dLon = (lon2 - lon1) * kRad;
    double y = sin(dLon) * cos(lat2 * kRad);
    double x = cos(lat1 * kRad) * sin(lat2 * kRad) -
               sin(lat1 * kRad) * cos(lat2 * kRad) * cos(dLon);
    double b = atan2(y, x) * kDeg;
    if (b < 0) b += 360;
    return b;
}

double elevationDeg(double distToSatKm, double satAltKm) {
    double central = distToSatKm / kEarthKm;
    double e = atan2(cos(central) - kEarthKm / (kEarthKm + satAltKm), sin(central));
    return e * kDeg;
}

const char *cardinal(double deg) {
    static const char *names[] = {"С", "СВ", "В", "ЮВ", "Ю", "ЮЗ", "З", "СЗ"};
    int i = (int)((deg + 22.5) / 45.0) & 7;
    return names[i];
}

void fmtCoord(char *out, size_t n, double v, bool lat) {
    char hemi = lat ? (v >= 0 ? 'N' : 'S') : (v >= 0 ? 'E' : 'W');
    double a = fabs(v);
    int d = (int)a;
    double m = (a - d) * 60.0;
    snprintf(out, n, "%d°%05.2f'%c", d, m, hemi);
}

void drawArrow(int cx, int cy, double ang, int len, uint16_t col) {
    double a = ang * kRad;
    int x = cx + (int)lround(sin(a) * len);
    int y = cy - (int)lround(cos(a) * len);
    gfx::line(cx, cy, x, y, col);

    int bx = x - (int)lround(sin(a) * 9);
    int by = y + (int)lround(cos(a) * 9);
    int px = (int)lround(cos(a) * 5);
    int py = (int)lround(sin(a) * 5);
    gfx::triangle(x, y, bx + px, by + py, bx - px, by - py, col, true);
}

void drawRadar(int cx, int cy, int r, double rangeKm, bool labels) {
    gfx::circle(cx, cy, r, gfx::col::PANEL3);
    gfx::circle(cx, cy, r * 2 / 3, gfx::col::PANEL2);
    gfx::circle(cx, cy, r / 3, gfx::col::PANEL);
    gfx::hline(cx - r, cy, r * 2, gfx::col::PANEL2);
    gfx::vline(cx, cy - r, r * 2, gfx::col::PANEL2);
    if (labels) {
        gfx::text5(cx - 2, cy - r - 10, "N", gfx::col::DIM, gfx::col::BG);
        gfx::text5(cx - 2, cy + r + 4, "S", gfx::col::DIM, gfx::col::BG);
        gfx::text5(cx + r + 4, cy - 3, "E", gfx::col::DIM, gfx::col::BG);
        gfx::text5(cx - r - 10, cy - 3, "W", gfx::col::DIM, gfx::col::BG);
        char b[16];
        snprintf(b, sizeof(b), "%.0f", rangeKm);
        gfx::text5(cx + r - 20, cy - r + 4, b, gfx::col::DIM2, gfx::col::BG);
    }
}

void drawLoading(const char *title, const char *line) {
    const uint16_t bg = gfx::col::BG;
    gfx::fillRect(0, UI_CONTENT_Y0, UI_W, UI_CONTENT_Y1 - UI_CONTENT_Y0, bg);
    gfx::drawIcon(gfx::Icon::Web, 104, UI_CONTENT_Y0 + 74, 32, gfx::col::accentDim());
    gfx::textCenter(120, UI_CONTENT_Y0 + 140, title, gfx::F_BODY_B,
                    gfx::col::accent, bg);
    gfx::textCenter(120, UI_CONTENT_Y0 + 164, line, gfx::F_SMALL,
                    gfx::col::TEXT2, bg);
    gfx::textCenter(120, UI_CONTENT_Y0 + 202, "нужен интернет", gfx::F_SMALL,
                    gfx::col::DIM2, bg);
}

bool ensureOnline(const char *title) {
    if (net::online()) return true;
    drawLoading(title, "Подключение к Wi-Fi...");
    return net::ensure(15000);
}

const char *weatherDesc(int code) {
    if (code == 0) return "ясно";
    if (code <= 2) return "малооблачно";
    if (code == 3) return "облачно";
    if (code == 45 || code == 48) return "туман";
    if (code >= 51 && code <= 57) return "морось";
    if (code >= 61 && code <= 67) return "дождь";
    if (code >= 71 && code <= 77) return "снег";
    if (code >= 80 && code <= 82) return "ливень";
    if (code == 85 || code == 86) return "снегопад";
    if (code >= 95) return "гроза";
    return "осадки";
}

bool gpsOrMinsk(double &lat, double &lon) {
    if (gps::hasFix()) {
        lat = gps::fix().lat;
        lon = gps::fix().lon;
        return true;
    }
    lat = kMinskLat;
    lon = kMinskLon;
    return false;
}

void openWeatherScreen();
void openPlanesScreen();
void openIssScreen();
void openCompassScreen();

class OnlineScreen : public Screen {
public:
    OnlineScreen() = default;
    const char *title() const override { return "Онлайн"; }
    const char *hint() const override { return "< Назад   OK открыть   ^v выбор"; }

    void enter() override {
        net::begin();
        if (net::hasCreds()) net::start();
        lastDraw_ = 0;
    }

    void leave() override { net::off(); }

    void resume() override {
        if (net::hasCreds() && !net::online()) net::start();
    }

    void tick() override {
        net::tick();
        if (net::state() == net::State::Connecting && millis() - lastDraw_ > 600) {
            lastDraw_ = millis();
            app::invalidate();
        }
    }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        int y = UI_CONTENT_Y0 + 4;

        gfx::fillRoundRect(8, y, 224, 60, 8, gfx::col::PANEL);
        gfx::drawIcon(gfx::Icon::Wifi, 18, y + 12, 28,
                      net::online() ? gfx::col::OK : gfx::col::accent);
        char ssid[30];
        if (net::hasCreds())
            gfx::ellipsize(ssid, sizeof(ssid), net::ssid().c_str(), 156,
                           gfx::F_SMALL_B);
        else
            snprintf(ssid, sizeof(ssid), "Wi-Fi не настроен");
        gfx::text(54, y + 22, ssid, gfx::F_SMALL_B, gfx::col::TEXT,
                  gfx::col::PANEL);
        gfx::text(54, y + 40, net::stateText(), gfx::F_SMALL,
                  net::online() ? gfx::col::OK
                                : net::state() == net::State::Failed ? gfx::col::ERR
                                                                    : gfx::col::WARN,
                  gfx::col::PANEL);
        char info[46];
        if (net::online())
            snprintf(info, sizeof(info), "%s  %d dBm", net::ip().c_str(), net::rssi());
        else if (!net::hasCreds())
            snprintf(info, sizeof(info), "создайте /wifi.txt на SD");
        else
            snprintf(info, sizeof(info), "проверьте ssid/pass");
        gfx::text(54, y + 56, info, gfx::F_SMALL, gfx::col::DIM, gfx::col::PANEL);

        y += 68;
        static const char *labels[] = {"Соединение", "Погода", "Самолёты",
                                       "МКС / спутник", "Компас"};
        static const gfx::Icon icons[] = {gfx::Icon::Wifi, gfx::Icon::Web,
                                          gfx::Icon::Radar, gfx::Icon::Star,
                                          gfx::Icon::Crosshair};
        const int rowH = 29;
        for (int i = 0; i < 5; i++) {
            int ry = y + i * rowH;
            bool sel = i == cursor_;
            uint16_t rbg = sel ? gfx::col::PANEL2 : bg;
            if (sel) {
                gfx::fillRoundRect(4, ry, 232, rowH - 2, 5, gfx::col::PANEL2);
                gfx::fillRect(4, ry + 4, 3, rowH - 10, gfx::col::accent);
            }
            gfx::drawIcon(icons[i], 14, ry + 5, 18,
                          sel ? gfx::col::accent : gfx::col::DIM);
            gfx::text(40, ry + 18, labels[i], gfx::F_SMALL,
                      gfx::col::TEXT, rbg);
            const char *v = "";
            if (i == 0) v = net::stateText();
            else if (!net::online()) v = "Wi-Fi";
            if (v[0]) gfx::textRight(230, ry + 18, v, gfx::F_SMALL,
                                     net::online() ? gfx::col::OK : gfx::col::DIM,
                                     rbg);
        }

        if (!net::hasCreds()) {
            int iy = y + 5 * rowH + 3;
            gfx::text(12, iy + 12, "Создайте /wifi.txt на SD:", gfx::F_SMALL,
                      gfx::col::TEXT2, bg);
            gfx::text(12, iy + 30, "ssid=МояСеть", gfx::F_SMALL,
                      gfx::col::accent, bg);
            gfx::text(12, iy + 46, "pass=МойПароль", gfx::F_SMALL,
                      gfx::col::accent, bg);
        }
    }

    bool onKey(Key k, KeyEvent e) override {
        if (k == Key::Up && (e == KeyEvent::Press || e == KeyEvent::Repeat)) {
            cursor_ = (cursor_ + 4) % 5;
            return true;
        }
        if (k == Key::Down && (e == KeyEvent::Press || e == KeyEvent::Repeat)) {
            cursor_ = (cursor_ + 1) % 5;
            return true;
        }
        if (k == Key::Ok && e == KeyEvent::Press) {
            if (cursor_ == 0) {
                net::off();
                net::begin();
                if (net::hasCreds()) {
                    net::start();
                    app::toast("подключение к %s", net::ssid().c_str());
                } else {
                    app::message("Wi-Fi не настроен",
                                 "Создайте на microSD файл /wifi.txt:\n"
                                 "ssid=МояСеть\npass=МойПароль\n"
                                 "Затем нажмите OK снова.");
                }
                return true;
            }
            if (!net::online()) {
                app::message("Нужен интернет",
                             "Подключитесь к Wi-Fi в первом пункте меню. "
                             "Логин и пароль задаются в /wifi.txt на SD.");
                return true;
            }
            buzzer::ok();
            switch (cursor_) {
            case 1: openWeatherScreen(); break;
            case 2: openPlanesScreen(); break;
            case 3: openIssScreen(); break;
            case 4: openCompassScreen(); break;
            default: break;
            }
            return true;
        }
        return false;
    }

private:
    int      cursor_ = 0;
    uint32_t lastDraw_ = 0;
};

class WeatherScreen : public Screen {
public:
    const char *title() const override { return "Погода"; }
    const char *hint() const override { return "< Назад   OK обновить"; }

    void enter() override { refresh(); }
    void draw() override;

    bool onKey(Key k, KeyEvent e) override {
        if (k == Key::Ok && e == KeyEvent::Press) {
            refresh();
            return true;
        }
        return false;
    }

private:
    bool   loaded_ = false;
    String err_;
    double temp_ = 0, feels_ = 0, wind_ = 0, windDir_ = 0, precip_ = 0;
    int    hum_ = 0, code_ = 0, press_ = 0;
    double lat_ = kMinskLat, lon_ = kMinskLon;
    bool   gps_ = false;

    void refresh() {
        loaded_ = false;
        err_ = "";
        drawLoading("Погода", "Запрос open-meteo.com...");
        if (!ensureOnline("Погода")) {
            err_ = "Нет интернета: ";
            err_ += net::errorText();
            app::invalidate();
            return;
        }
        gps_ = gpsOrMinsk(lat_, lon_);
        char url[280];
        snprintf(url, sizeof(url),
                 "https://api.open-meteo.com/v1/forecast?latitude=%.5f&longitude=%.5f"
                 "&current=temperature_2m,apparent_temperature,relative_humidity_2m,"
                 "precipitation,weather_code,pressure_msl,wind_speed_10m,"
                 "wind_direction_10m",
                 lat_, lon_);
        String body;
        if (!net::httpGet(url, body)) {
            err_ = "Ошибка запроса: ";
            err_ += net::errorText();
            app::invalidate();
            return;
        }
        if (!net::jsonNumber(body, "temperature_2m", temp_)) {
            err_ = "Сервер вернул неожиданный ответ";
            app::invalidate();
            return;
        }
        net::jsonNumber(body, "apparent_temperature", feels_);
        net::jsonNumber(body, "wind_speed_10m", wind_);
        net::jsonNumber(body, "wind_direction_10m", windDir_);
        net::jsonNumber(body, "precipitation", precip_);
        net::jsonInt(body, "relative_humidity_2m", hum_);
        net::jsonInt(body, "weather_code", code_);
        net::jsonInt(body, "pressure_msl", press_);
        loaded_ = true;
        klog::add("weather: %.1f C", temp_);
        app::invalidate();
    }
};

void WeatherScreen::draw() {
    const uint16_t bg = gfx::col::BG;
    int y = UI_CONTENT_Y0 + 6;
    if (!loaded_) {
        gfx::fillRoundRect(10, y + 20, 220, 80, 8, gfx::col::PANEL);
        gfx::textCenter(120, y + 56, "Погода недоступна", gfx::F_BODY_B,
                        gfx::col::ERR, gfx::col::PANEL);
        char b[128];
        gfx::ellipsize(b, sizeof(b), err_.c_str(), 204, gfx::F_SMALL);
        gfx::textCenter(120, y + 80, b, gfx::F_SMALL, gfx::col::DIM,
                        gfx::col::PANEL);
        return;
    }

    gfx::fillRoundRect(8, y, 224, 92, 8, gfx::col::PANEL);
    char b[40];
    snprintf(b, sizeof(b), "%+.1f°", temp_);
    gfx::text(18, y + 48, b, gfx::F_BIG_B, gfx::col::accent, gfx::col::PANEL);
    gfx::text(18, y + 70, weatherDesc(code_), gfx::F_SMALL, gfx::col::TEXT2,
              gfx::col::PANEL);
    snprintf(b, sizeof(b), "ощущ. %+.1f°", feels_);
    gfx::textRight(220, y + 30, b, gfx::F_SMALL, gfx::col::TEXT2,
                   gfx::col::PANEL);
    snprintf(b, sizeof(b), "влажн. %d%%", hum_);
    gfx::textRight(220, y + 48, b, gfx::F_SMALL, gfx::col::DIM,
                   gfx::col::PANEL);
    snprintf(b, sizeof(b), "%d гПа", press_);
    gfx::textRight(220, y + 66, b, gfx::F_SMALL, gfx::col::DIM,
                   gfx::col::PANEL);

    y += 102;
    const int rowH = 24;
    char lat[24], lon[24];
    fmtCoord(lat, sizeof(lat), lat_, true);
    fmtCoord(lon, sizeof(lon), lon_, false);
    struct Row { const char *k; char v[48]; };
    Row rows[4];
    snprintf(rows[0].v, sizeof(rows[0].v), "%.1f км/ч  %s (%d°)", wind_,
             cardinal(windDir_), (int)windDir_);
    rows[0].k = "Ветер";
    snprintf(rows[1].v, sizeof(rows[1].v), "%.1f мм", precip_);
    rows[1].k = "Осадки";
    snprintf(rows[2].v, sizeof(rows[2].v), "%s  %s", lat, lon);
    rows[2].k = "Точка";
    snprintf(rows[3].v, sizeof(rows[3].v), "%s",
             gps_ ? "GPS" : "Минск (нет GPS-фикса)");
    rows[3].k = "Источник";
    for (int i = 0; i < 4; i++) {
        uint16_t rbg = (i % 2) ? bg : gfx::col::BG2;
        gfx::fillRect(8, y, 224, rowH - 2, rbg);
        gfx::text(14, y + 15, rows[i].k, gfx::F_SMALL, gfx::col::DIM, rbg);
        gfx::textRight(226, y + 15, rows[i].v, gfx::F_SMALL, gfx::col::TEXT, rbg);
        y += rowH;
    }
    gfx::text5(12, UI_CONTENT_Y1 - 16, "open-meteo.com  WMO", gfx::col::DIM2, bg);
}

struct Plane {
    String call;
    double lat = 0, lon = 0, alt = 0, gs = 0, trk = 0, dist = 0, brg = 0;
    bool   ground = false;
};

class PlanesScreen : public Screen {
public:
    const char *title() const override { return "Карта самолётов"; }
    const char *hint() const override {
        return "< Назад   OK обновить   ^v выбор";
    }

    void enter() override {
        gpsOrMinsk(cLat_, cLon_);
        refresh();
    }

    void draw() override;
    bool onKey(Key k, KeyEvent e) override;

private:
    std::vector<Plane> planes_;
    int      sel_ = 0;
    bool     loaded_ = false;
    String   err_;
    double   cLat_ = kMinskLat, cLon_ = kMinskLon;
    uint32_t at_ = 0;

    void refresh();
};

int objEnd(const String &s, int start) {
    int depth = 0;
    bool inStr = false, esc = false;
    for (int i = start; i < (int)s.length(); i++) {
        char c = s[i];
        if (inStr) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') inStr = false;
        } else {
            if (c == '"') inStr = true;
            else if (c == '{') depth++;
            else if (c == '}') {
                depth--;
                if (depth == 0) return i;
            }
        }
    }
    return -1;
}

void PlanesScreen::refresh() {
    loaded_ = false;
    err_ = "";
    drawLoading("Самолёты", "Запрос ADS-B (adsb.lol)...");
    if (!ensureOnline("Самолёты")) {
        err_ = "Нет интернета: ";
        err_ += net::errorText();
        app::invalidate();
        return;
    }
    char url[180];
    snprintf(url, sizeof(url), "https://api.adsb.lol/v2/point/%.5f/%.5f/60", cLat_,
             cLon_);
    String body;
    if (!net::httpGet(url, body, 15000)) {
        err_ = "Ошибка запроса: ";
        err_ += net::errorText();
        app::invalidate();
        return;
    }

    planes_.clear();
    int ac = body.indexOf("\"ac\"");
    int p = ac >= 0 ? body.indexOf('[', ac) : -1;
    int guard = 0;
    while (p >= 0 && p < (int)body.length() && guard++ < 80) {
        int st = body.indexOf('{', p);
        if (st < 0) break;
        int en = objEnd(body, st);
        if (en < 0) break;
        String o = body.substring(st, en + 1);
        double lat = 0, lon = 0;
        if (net::jsonNumber(o, "lat", lat) && net::jsonNumber(o, "lon", lon)) {
            Plane pl;
            pl.lat = lat;
            pl.lon = lon;
            pl.call = net::jsonString(o, "flight");
            pl.call.trim();
            if (!pl.call.length()) pl.call = net::jsonString(o, "hex");
            pl.call.trim();
            if (pl.call.length() > 9) pl.call = pl.call.substring(0, 9);
            if (!net::jsonNumber(o, "alt_baro", pl.alt)) {
                pl.ground = o.indexOf("\"alt_baro\":\"ground\"") >= 0;
                pl.alt = 0;
            }
            net::jsonNumber(o, "gs", pl.gs);
            net::jsonNumber(o, "track", pl.trk);
            pl.dist = distanceKm(cLat_, cLon_, pl.lat, pl.lon);
            pl.brg = bearingDeg(cLat_, cLon_, pl.lat, pl.lon);
            if (pl.dist <= 75) planes_.push_back(pl);
        }
        p = en + 1;
    }

    for (size_t i = 1; i < planes_.size(); i++) {
        Plane key = planes_[i];
        int j = (int)i - 1;
        while (j >= 0 && planes_[j].dist > key.dist) {
            planes_[j + 1] = planes_[j];
            j--;
        }
        planes_[j + 1] = key;
    }
    sel_ = 0;
    loaded_ = true;
    at_ = millis();
    klog::add("adsb: %d planes", (int)planes_.size());
    app::invalidate();
}

void PlanesScreen::draw() {
    const uint16_t bg = gfx::col::BG;
    int cx = 120, cy = 132, R = 84;
    if (!loaded_) {
        gfx::textCenter(120, UI_CONTENT_Y0 + 70, "Нет данных ADS-B",
                        gfx::F_BODY_B, gfx::col::ERR, bg);
        char b[128];
        gfx::ellipsize(b, sizeof(b), err_.c_str(), 212, gfx::F_SMALL);
        gfx::textCenter(120, UI_CONTENT_Y0 + 94, b, gfx::F_SMALL, gfx::col::DIM,
                        bg);
        return;
    }

    char b[64];
    snprintf(b, sizeof(b), "радиус 60 км: %d", (int)planes_.size());
    gfx::text(10, UI_CONTENT_Y0 + 14, b, gfx::F_SMALL, gfx::col::TEXT2, bg);

    drawRadar(cx, cy, R, 60, true);

    for (int i = 0; i < (int)planes_.size(); i++) {
        const Plane &pl = planes_[i];
        if (pl.dist > 60) continue;
        double a = pl.brg * kRad;
        int x = cx + (int)lround(sin(a) * pl.dist / 60 * R);
        int y = cy - (int)lround(cos(a) * pl.dist / 60 * R);
        uint16_t c = (i == sel_) ? gfx::col::accent : gfx::col::TEXT2;

        double h = pl.trk * kRad;
        int fx = (int)lround(sin(h) * 6), fy = -(int)lround(cos(h) * 6);
        int sx = (int)lround(cos(h) * 4), sy = (int)lround(sin(h) * 4);
        gfx::triangle(x + fx, y + fy, x - fx / 2 + sx, y - fy / 2 + sy,
                      x - fx / 2 - sx, y - fy / 2 - sy, c, true);
    }

    int y = 230;
    gfx::hline(8, y - 8, 224, gfx::col::LINE);
    if (planes_.empty()) {
        gfx::textCenter(120, y + 14, "В радиусе 60 км", gfx::F_BODY_B,
                        gfx::col::DIM, bg);
        gfx::textCenter(120, y + 36, "никого нет", gfx::F_BODY_B,
                        gfx::col::DIM, bg);
        gfx::textCenter(120, y + 56, "OK = обновить", gfx::F_SMALL,
                        gfx::col::DIM2, bg);
        return;
    }
    const Plane &pl = planes_[sel_];
    gfx::text(12, y + 18, pl.call.length() ? pl.call.c_str() : "без позывного",
              gfx::F_BODY_B, gfx::col::accent, bg);
    char alt[32];
    if (pl.ground) snprintf(alt, sizeof(alt), "на земле");
    else snprintf(alt, sizeof(alt), "%.0f фт", pl.alt);
    snprintf(b, sizeof(b), "%s   %.0f км/ч", alt, pl.gs * 1.852);
    gfx::text(12, y + 38, b, gfx::F_SMALL, gfx::col::TEXT2, bg);
    snprintf(b, sizeof(b), "%.1f км  %s (%d°)", pl.dist, cardinal(pl.brg),
             (int)pl.brg);
    gfx::text(12, y + 54, b, gfx::F_SMALL, gfx::col::DIM, bg);
}

bool PlanesScreen::onKey(Key k, KeyEvent e) {
    if (k == Key::Ok && e == KeyEvent::Press) {
        refresh();
        return true;
    }
    if (!planes_.empty()) {
        if (k == Key::Up && (e == KeyEvent::Press || e == KeyEvent::Repeat)) {
            sel_ = (sel_ - 1 + (int)planes_.size()) % (int)planes_.size();
            return true;
        }
        if (k == Key::Down && (e == KeyEvent::Press || e == KeyEvent::Repeat)) {
            sel_ = (sel_ + 1) % (int)planes_.size();
            return true;
        }
    }
    return false;
}

class IssScreen : public Screen {
public:
    const char *title() const override { return "МКС / спутник"; }
    const char *hint() const override { return "< Назад   OK обновить"; }

    void enter() override { refresh(); }
    void draw() override;

    bool onKey(Key k, KeyEvent e) override {
        if (k == Key::Ok && e == KeyEvent::Press) {
            refresh();
            return true;
        }
        return false;
    }

private:
    bool     loaded_ = false;
    String   err_;
    double   lat_ = 0, lon_ = 0, alt_ = 0, vel_ = 0;
    double   dist_ = 0, brg_ = 0, elev_ = 0;
    double   uLat_ = kMinskLat, uLon_ = kMinskLon;
    String   vis_;
    uint32_t at_ = 0;

    void refresh() {
        loaded_ = false;
        err_ = "";
        drawLoading("МКС", "Запрос wheretheiss.at...");
        if (!ensureOnline("МКС")) {
            err_ = "Нет интернета: ";
            err_ += net::errorText();
            app::invalidate();
            return;
        }
        gpsOrMinsk(uLat_, uLon_);
        String body;
        if (!net::httpGet("https://api.wheretheiss.at/v1/satellites/25544", body,
                          12000)) {
            err_ = "Ошибка запроса: ";
            err_ += net::errorText();
            app::invalidate();
            return;
        }
        if (!net::jsonNumber(body, "latitude", lat_) ||
            !net::jsonNumber(body, "longitude", lon_)) {
            err_ = "Сервер вернул неожиданный ответ";
            app::invalidate();
            return;
        }
        net::jsonNumber(body, "altitude", alt_);
        net::jsonNumber(body, "velocity", vel_);
        vis_ = net::jsonString(body, "visibility");
        dist_ = distanceKm(uLat_, uLon_, lat_, lon_);
        brg_ = bearingDeg(uLat_, uLon_, lat_, lon_);
        elev_ = elevationDeg(dist_, alt_);
        loaded_ = true;
        at_ = millis();
        klog::add("iss: %.1f,%.1f", lat_, lon_);
        app::invalidate();
    }
};

void IssScreen::draw() {
    const uint16_t bg = gfx::col::BG;
    if (!loaded_) {
        gfx::textCenter(120, UI_CONTENT_Y0 + 70, "Спутник недоступен",
                        gfx::F_BODY_B, gfx::col::ERR, bg);
        char b[128];
        gfx::ellipsize(b, sizeof(b), err_.c_str(), 212, gfx::F_SMALL);
        gfx::textCenter(120, UI_CONTENT_Y0 + 94, b, gfx::F_SMALL, gfx::col::DIM,
                        bg);
        return;
    }

    const int cx = 120, cy = 132, R = 84;
    bool over = elev_ > 0;
    char b[64];
    snprintf(b, sizeof(b), "МКС  %s", over ? "над горизонтом" : "под горизонтом");
    gfx::text(10, UI_CONTENT_Y0 + 14, b, gfx::F_SMALL,
              over ? gfx::col::OK : gfx::col::DIM, bg);

    drawRadar(cx, cy, R, 2500, true);
    double a = brg_ * kRad;
    double rr = dist_ / 2500.0 * R;
    if (rr > R) rr = R;
    int x = cx + (int)lround(sin(a) * rr);
    int y = cy - (int)lround(cos(a) * rr);
    gfx::fillCircle(x, y, 5, over ? gfx::col::accent : gfx::col::DIM2);
    gfx::circle(x, y, 9, over ? gfx::col::WARN : gfx::col::PANEL3);

    int ty = 230;
    gfx::hline(8, ty - 8, 224, gfx::col::LINE);
    char lat[24], lon[24];
    fmtCoord(lat, sizeof(lat), lat_, true);
    fmtCoord(lon, sizeof(lon), lon_, false);
    gfx::text(12, ty + 16, "МКС", gfx::F_BODY_B, gfx::col::accent, bg);
    gfx::textRight(232, ty + 16, vis_ == "daylight" ? "на солнце" : "в тени",
                   gfx::F_SMALL, gfx::col::TEXT2, bg);
    snprintf(b, sizeof(b), "%s  %s", lat, lon);
    gfx::text(12, ty + 34, b, gfx::F_SMALL, gfx::col::TEXT2, bg);
    snprintf(b, sizeof(b), "%.0f км  %s  выс. %.0f км", dist_, cardinal(brg_),
             alt_);
    gfx::text(12, ty + 50, b, gfx::F_SMALL, gfx::col::DIM, bg);
}

class CompassScreen : public Screen {
public:
    const char *title() const override { return "Компас (онлайн)"; }
    const char *hint() const override { return "< Назад   OK обновить"; }

    void enter() override { refresh(); }
    void draw() override;

    bool onKey(Key k, KeyEvent e) override {
        if (k == Key::Ok && e == KeyEvent::Press) {
            refresh();
            return true;
        }
        return false;
    }

private:
    bool   loaded_ = false;
    String err_;
    double temp_ = 0, wind_ = 0, windDir_ = 0;
    int    code_ = 0;
    double lat_ = kMinskLat, lon_ = kMinskLon;
    bool   gps_ = false;

    void refresh() {
        loaded_ = false;
        err_ = "";
        drawLoading("Компас", "Запрос направления ветра...");
        if (!ensureOnline("Компас")) {
            err_ = "Компас работает только с интернетом. ";
            err_ += net::errorText();
            app::invalidate();
            return;
        }
        gps_ = gpsOrMinsk(lat_, lon_);
        char url[260];
        snprintf(url, sizeof(url),
                 "https://api.open-meteo.com/v1/forecast?latitude=%.5f&longitude=%.5f"
                 "&current=temperature_2m,weather_code,wind_speed_10m,"
                 "wind_direction_10m",
                 lat_, lon_);
        String body;
        if (!net::httpGet(url, body)) {
            err_ = "Ошибка запроса: ";
            err_ += net::errorText();
            app::invalidate();
            return;
        }
        if (!net::jsonNumber(body, "wind_direction_10m", windDir_)) {
            err_ = "Сервер вернул неожиданный ответ";
            app::invalidate();
            return;
        }
        net::jsonNumber(body, "wind_speed_10m", wind_);
        net::jsonNumber(body, "temperature_2m", temp_);
        net::jsonInt(body, "weather_code", code_);
        loaded_ = true;
        app::invalidate();
    }
};

void CompassScreen::draw() {
    const uint16_t bg = gfx::col::BG;
    if (!loaded_) {
        gfx::fillRoundRect(10, UI_CONTENT_Y0 + 30, 220, 92, 8, gfx::col::PANEL);
        gfx::textCenter(120, UI_CONTENT_Y0 + 64, "Компас недоступен",
                        gfx::F_BODY_B, gfx::col::ERR, gfx::col::PANEL);
        char b[128];
        gfx::ellipsize(b, sizeof(b), err_.c_str(), 204, gfx::F_SMALL);
        gfx::textCenter(120, UI_CONTENT_Y0 + 88, b, gfx::F_SMALL,
                        gfx::col::DIM, gfx::col::PANEL);
        gfx::textCenter(120, UI_CONTENT_Y0 + 112, "Проверьте /wifi.txt на SD",
                        gfx::F_SMALL, gfx::col::DIM2, gfx::col::PANEL);
        return;
    }

    const int cx = 120, cy = 150, R = 86;
    double course = gps::hasFix() ? gps::fix().course : windDir_;
    bool haveCourse = gps::hasFix();

    for (int d = 0; d < 360; d += 30) {
        double a = d * kRad;
        int x0 = cx + (int)lround(sin(a) * (R - 7));
        int y0 = cy - (int)lround(cos(a) * (R - 7));
        int x1 = cx + (int)lround(sin(a) * R);
        int y1 = cy - (int)lround(cos(a) * R);
        gfx::line(x0, y0, x1, y1, gfx::col::PANEL3);
    }
    gfx::circle(cx, cy, R, gfx::col::PANEL3);
    gfx::circle(cx, cy, R * 2 / 3, gfx::col::PANEL2);
    gfx::circle(cx, cy, R / 3, gfx::col::PANEL);
    gfx::textCenter(cx, cy - R + 15, "С", gfx::F_SMALL_B, gfx::col::TEXT2, bg);
    gfx::textCenter(cx, cy + R - 3, "Ю", gfx::F_SMALL_B, gfx::col::TEXT2, bg);
    gfx::text(cx + R - 14, cy + 5, "В", gfx::F_SMALL_B, gfx::col::TEXT2, bg);
    gfx::textRight(cx - R + 14, cy + 5, "З", gfx::F_SMALL_B, gfx::col::TEXT2, bg);

    drawArrow(cx, cy, windDir_, R - 10, gfx::col::BLUE);

    if (haveCourse) drawArrow(cx, cy, course, R - 26, gfx::col::accent);

    gfx::fillCircle(cx, cy, 4, gfx::col::TEXT);
    char b[40];
    snprintf(b, sizeof(b), "%d°", (int)lround(course));
    gfx::textCenter(cx, cy + 34, b, gfx::F_BIG_B, gfx::col::accent, bg);
    gfx::textCenter(cx, cy + 50, haveCourse ? "курс GPS" : "ветер",
                    gfx::F_SMALL, gfx::col::DIM, bg);

    int y = 242;
    gfx::hline(8, y, 224, gfx::col::LINE);
    snprintf(b, sizeof(b), "ветер %.1f м/с, %s", wind_, cardinal(windDir_));
    gfx::text(12, y + 16, b, gfx::F_SMALL, gfx::col::BLUE, bg);
    snprintf(b, sizeof(b), "%+.1f°C, %s", temp_, weatherDesc(code_));
    gfx::text(12, y + 32, b, gfx::F_SMALL, gfx::col::TEXT2, bg);
    gfx::text(12, y + 48, gps_ ? "координаты: GPS" : "координаты: Минск",
              gfx::F_SMALL, gfx::col::DIM, bg);
}

void openWeatherScreen() { app::push(new WeatherScreen()); }
void openPlanesScreen() { app::push(new PlanesScreen()); }
void openIssScreen() { app::push(new IssScreen()); }
void openCompassScreen() { app::push(new CompassScreen()); }

}

namespace screens {

void openOnline() { app::push(new OnlineScreen()); }

}
