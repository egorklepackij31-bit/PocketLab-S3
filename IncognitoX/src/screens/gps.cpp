#include <algorithm>
#include <math.h>
#include <time.h>
#include <vector>

#include "app/app.h"
#include "app/log.h"
#include "app/settings.h"
#include "app/ui.h"
#include "config.h"
#include "hw/buzzer.h"
#include "hw/display.h"
#include "hw/gfx.h"
#include "hw/gps.h"
#include "hw/input.h"
#include "hw/storage.h"
#include "screens/screens.h"

namespace {

#define GPS_DIR "/gps"

const char *fixQualityName(uint8_t q) {
    switch (q) {
    case 1: return "GPS fix";
    case 2: return "DGPS fix";
    case 3: return "PPS fix";
    case 4: return "RTK fixed";
    case 5: return "RTK float";
    default: return "no fix";
    }
}

void fmtCoord(char *out, size_t n, double v, bool lat) {
    char hemi = lat ? (v >= 0 ? 'N' : 'S') : (v >= 0 ? 'E' : 'W');
    double a = fabs(v);
    int deg = (int)a;
    double min = (a - deg) * 60.0;
    snprintf(out, n, "%d %07.4f %c", deg, min, hemi);
}

class GpsLive : public Screen {
public:
    const char *title() const override { return "GPS в реальном времени"; }
    const char *hint() const override { return "< Назад   OK установить время"; }

    void tick() override {
        if (millis() - lastDraw_ < 400) return;
        lastDraw_ = millis();
        app::invalidate();
    }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        const GpsFix &f = gps::fix();
        bool fix = gps::hasFix();

        int y = UI_CONTENT_Y0 + 6;
        gfx::fillRoundRect(8, y, 224, 46, 8, gfx::col::PANEL);
        gfx::drawIcon(gfx::Icon::Gps, 18, y + 10, 28,
                      fix ? gfx::col::OK : gfx::col::WARN);
        gfx::text(58, y + 20, fix ? "3D ФИКС" : "НЕТ ФИКСА", gfx::F_BODY_B,
                  fix ? gfx::col::OK : gfx::col::WARN, gfx::col::PANEL);
        char b[64];
        snprintf(b, sizeof(b), "%u sats used / %u in view   hdop %.1f", f.sats,
                 f.satsInView, f.hdop);
        gfx::text(58, y + 38, b, gfx::F_SMALL, gfx::col::TEXT2, gfx::col::PANEL);

        y += 56;
        char lat[28], lon[28];
        fmtCoord(lat, sizeof(lat), f.lat, true);
        fmtCoord(lon, sizeof(lon), f.lon, false);
        gfx::text(12, y + 18, "ШИРОТА", gfx::F_SMALL, gfx::col::DIM, bg);
        gfx::text(70, y + 18, lat, gfx::F_BODY_B, gfx::col::TEXT, bg);
        gfx::text(12, y + 42, "ДОЛГОТА", gfx::F_SMALL, gfx::col::DIM, bg);
        gfx::text(70, y + 42, lon, gfx::F_BODY_B, gfx::col::TEXT, bg);

        y += 56;
        snprintf(b, sizeof(b), "Alt %.1f m    Speed %.1f km/h", f.alt, f.speedKmh);
        gfx::text(12, y + 12, b, gfx::F_SMALL, gfx::col::TEXT2, bg);
        snprintf(b, sizeof(b), "Course %.0f deg    Quality %s", f.course,
                 fixQualityName(f.fixQuality));
        gfx::text(12, y + 30, b, gfx::F_SMALL, gfx::col::TEXT2, bg);

        if (f.year >= 2020)
            snprintf(b, sizeof(b), "UTC %04u-%02u-%02u  %02u:%02u:%02u", f.year,
                     f.month, f.day, f.hour, f.minute, f.second);
        else
            snprintf(b, sizeof(b), "UTC time not received yet");
        gfx::text(12, y + 48, b, gfx::F_SMALL, gfx::col::DIM, bg);

        y += 58;
        gfx::text(12, y + 12, gps::present() ? "поток NMEA активен" : "нет данных NMEA",
                  gfx::F_SMALL, gps::present() ? gfx::col::DIM : gfx::col::WARN, bg);
        snprintf(b, sizeof(b), "%lu sentences   last byte %lu ms ago",
                 (unsigned long)gps::sentences(),
                 (unsigned long)(millis() - gps::lastByteMs()));
        gfx::text(12, y + 28, b, gfx::F_SMALL, gfx::col::DIM, bg);
    }

    bool onKey(Key k, KeyEvent e) override {
        if (k == Key::Ok && e == KeyEvent::Press) {
            if (!gps::hasFix()) {
                app::toast("no GPS fix yet");
                return true;
            }
            gps::syncClock(settings.tz);
            buzzer::ok();
            app::toast("clock set from GPS");
            klog::add("gps: clock synced");
            return true;
        }
        return false;
    }

private:
    uint32_t lastDraw_ = 0;
};

class GpsSky : public Screen {
public:
    const char *title() const override { return "Качество привязки GPS"; }
    const char *hint() const override { return "< back"; }

    void tick() override {
        if (millis() - lastDraw_ < 500) return;
        lastDraw_ = millis();
        app::invalidate();
    }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        const GpsFix &f = gps::fix();
        int cx = 120, cy = 142, R = 66;
        gfx::circle(cx, cy, R, gfx::col::PANEL3);
        gfx::circle(cx, cy, R * 2 / 3, gfx::col::PANEL2);
        gfx::circle(cx, cy, R / 3, gfx::col::PANEL);
        gfx::text5(cx - 3, cy - R - 11, "N", gfx::col::DIM, bg);
        gfx::text5(cx - 3, cy + R + 3, "S", gfx::col::DIM, bg);
        gfx::text5(cx + R + 4, cy - 3, "E", gfx::col::DIM, bg);
        gfx::text5(cx - R - 10, cy - 3, "W", gfx::col::DIM, bg);

        if (f.valid) {
            float a = (f.course - 90.0f) * (float)M_PI / 180.0f;
            int x1 = cx + (int)(cosf(a) * (R - 6));
            int y1 = cy + (int)(sinf(a) * (R - 6));
            gfx::line(cx, cy, x1, y1, gfx::col::accent);
            gfx::fillCircle(x1, y1, 4, gfx::col::accent);
            gfx::fillCircle(cx, cy, 5, gfx::col::TEXT);
            char b[32];
            snprintf(b, sizeof(b), "%.0f", f.course);
            gfx::textCenter(cx, cy + 104, b, gfx::F_BODY_B, gfx::col::accent, bg);
            gfx::textCenter(cx, cy + 124, "курс", gfx::F_SMALL, gfx::col::DIM, bg);
        } else {
            gfx::textCenter(cx, cy + 2, "нет", gfx::F_BODY_B, gfx::col::DIM, bg);
            gfx::textCenter(cx, cy + 26, "исправления", gfx::F_BODY_B, gfx::col::DIM, bg);
        }

        char b[72];
        snprintf(b, sizeof(b), "спутники %u/%u   hdop %.1f   %s", f.sats,
                 f.satsInView, f.hdop, fixQualityName(f.fixQuality));
        gfx::text(10, 38, b, gfx::F_SMALL, gfx::col::TEXT2, bg);
        int q = constrain((int)((2.5f - f.hdop) * 40), 0, 100);
        if (!f.valid) q = 0;
        gfx::progressBar(10, 44, 200, 7, q, 100, gfx::col::accent, gfx::col::PANEL2);
        gfx::text(10, 56, f.hdop < 1.5f ? "hdop отличный" :
                          f.hdop < 2.5f ? "hdop хороший" :
                          f.hdop < 5.0f ? "hdop средний" : "hdop плохой",
                   gfx::F_SMALL, gfx::col::DIM, bg);
    }

    bool onKey(Key, KeyEvent) override { return false; }

private:
    uint32_t lastDraw_ = 0;
};

class GpsTrack : public Screen {
public:
    const char *title() const override { return "Запись трека"; }
    const char *hint() const override { return "< back   OK start / stop"; }

    void enter() override {
        if (!storage::mounted())
            app::message("No microSD", "A card is required to store the track.");
    }

    void leave() override { stop(); }

    void tick() override {
        if (!recording_) return;
        if (millis() - lastPoint_ < 1000) return;
        lastPoint_ = millis();
        const GpsFix &f = gps::fix();
        if (!gps::hasFix()) return;
        time_t now = time(nullptr);
        struct tm tmv;
        gmtime_r(&now, &tmv);
        char line[160];
        snprintf(line, sizeof(line),
                 "%04d-%02d-%02dT%02d:%02d:%02dZ,%.7f,%.7f,%.1f,%.1f,%.1f,%u,%u",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour,
                 tmv.tm_min, tmv.tm_sec, f.lat, f.lon, f.alt, f.speedKmh,
                 f.hdop, f.sats, f.fixQuality);
        storage::appendLine(path_, line);
        points_++;
        app::invalidate();
    }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        int y = UI_CONTENT_Y0 + 10;
        gfx::fillRoundRect(8, y, 224, 66, 8, gfx::col::PANEL);
        char b[32];
        snprintf(b, sizeof(b), "%lu", (unsigned long)points_);
        gfx::text(20, y + 44, b, gfx::F_BIG_B, gfx::col::accent, gfx::col::PANEL);
        gfx::text(20 + gfx::textWidth(b, gfx::F_BIG_B) + 10, y + 28, "точек",
                  gfx::F_SMALL, gfx::col::TEXT2, gfx::col::PANEL);
        gfx::text(20 + gfx::textWidth(b, gfx::F_BIG_B) + 10, y + 46,
                  recording_ ? "запись каждые 1 с" : "остановлено", gfx::F_SMALL,
                  recording_ ? gfx::col::OK : gfx::col::DIM, gfx::col::PANEL);
        gfx::drawIcon(gfx::Icon::Map, 200, y + 18, 32,
                      recording_ ? gfx::col::OK : gfx::col::DIM2);

        y += 78;
        gfx::text(12, y + 14, gps::hasFix() ? "GPS: фикс установлен" : "Фикс не получен",
                  gfx::F_SMALL,
                  gps::hasFix() ? gfx::col::OK : gfx::col::WARN, bg);
        y += 22;
        gfx::text(12, y + 12, path_[0] ? path_ : "OK = новый трек", gfx::F_SMALL,
                  gfx::col::TEXT2, bg);
        y += 20;
        gfx::text(12, y + 12, storage::mounted() ? "microSD подключена" : "нет microSD",
                  gfx::F_SMALL,
                  storage::mounted() ? gfx::col::DIM : gfx::col::ERR, bg);
        gfx::text5(12, y + 32, "CSV: time,lat,lon,alt,spd,hdop,sats,fix",
                   gfx::col::DIM, bg);
    }

    bool onKey(Key k, KeyEvent e) override {
        if (k == Key::Ok && e == KeyEvent::Press) {
            if (recording_) stop();
            else start();
            return true;
        }
        return false;
    }

private:
    bool     recording_ = false;
    uint32_t lastPoint_ = 0;
    uint32_t points_ = 0;
    char     path_[48] = {0};

    void start() {
        if (!storage::mounted()) { app::toast("no microSD"); return; }
        storage::ensureDir(GPS_DIR);
        time_t now = time(nullptr);
        struct tm tmv;
        gmtime_r(&now, &tmv);
        snprintf(path_, sizeof(path_), GPS_DIR "/track_%04d%02d%02d_%02d%02d.csv",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour,
                 tmv.tm_min);
        if (!storage::exists(path_))
            storage::writeText(path_, "utc,lat,lon,alt,speed,hdop,sats,fix\r\n");
        points_ = 0;
        recording_ = true;
        lastPoint_ = 0;
        buzzer::ok();
        klog::add("gps track: %s", path_);
        app::toast("recording");
    }

    void stop() {
        if (!recording_) return;
        recording_ = false;
        buzzer::click();
        app::toast("%lu points saved", (unsigned long)points_);
        app::invalidate();
    }
};

uint8_t  g_mapRow[4096 * 3];
uint16_t g_mapBuf[240 * 16];
constexpr int kMapViewH = UI_CONTENT_Y1 - UI_CONTENT_Y0;

double mercY(double lat) {
    double r = lat * (M_PI / 180.0);
    return log(tan(M_PI / 4.0 + r / 2.0));
}
double invMercY(double y) { return atan(sinh(y)) * (180.0 / M_PI); }

class MapView : public Screen {
public:
    explicit MapView(String path) : path_(std::move(path)) {
        int slash = path_.lastIndexOf('/');
        name_ = slash >= 0 ? path_.substring(slash + 1) : path_;
    }
    ~MapView() override { if (file_) file_.close(); }

    const char *title() const override { return "Карта"; }
    const char *hint() const override {
        return "< > ^v сдвиг  OK точка  удерж.OK зум";
    }

    void enter() override {
        open();
        loadWaypoints();
        if (ok_)
            app::toast("Удержание < = выход");
    }

    void leave() override {
        if (file_) file_.close();
    }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        if (!ok_) {
            gfx::text(12, UI_CONTENT_Y0 + 40, "Не открылась карта", gfx::F_BODY_B,
                      gfx::col::ERR, bg);
            gfx::text(12, UI_CONTENT_Y0 + 64, err_, gfx::F_SMALL, gfx::col::DIM,
                      bg);
            return;
        }
        render();
        overlays();
    }

    bool onKey(Key k, KeyEvent e) override {
        if (!ok_) return false;
        if (k == Key::Left && e == KeyEvent::Long) {
            app::pop();
            return true;
        }
        if (k == Key::Ok && e == KeyEvent::Long) {
            zoomIdx_ = (zoomIdx_ + 1) % 5;
            clampCenter();
            if (zoomIdx_ == 0) app::toast("вся карта");
            else app::toast("масштаб x%.2f", scale());
            return true;
        }
        if (k == Key::Ok && e == KeyEvent::Press) {
            addWaypoint();
            return true;
        }
        if (e == KeyEvent::Press || e == KeyEvent::Repeat) {
            int step = (e == KeyEvent::Repeat) ? 12 : 24;
            if (zoomIdx_ == 0) zoomIdx_ = 1;
            switch (k) {
            case Key::Left:  pan(-step, 0); return true;
            case Key::Right: pan(step, 0); return true;
            case Key::Up:    pan(0, -step); return true;
            case Key::Down:  pan(0, step); return true;
            default: break;
            }
        }
        return false;
    }

private:
    struct Wp {
        float  x = 0, y = 0;
        double lat = 0, lon = 0;
        bool   geo = false;
    };

    String   path_, name_;
    char     err_[64] = {0};
    File     file_;
    bool     ok_ = false;
    int      srcW_ = 0, srcH_ = 0;
    uint32_t dataOff_ = 0, rowSize_ = 0;
    bool     topDown_ = false;
    bool     hasGeo_ = false;
    double   latTop_ = 0, lonLeft_ = 0, latBottom_ = 0, lonRight_ = 0;
    double   cx_ = 0, cy_ = 0;
    int      zoomIdx_ = 0;
    std::vector<Wp> wps_;

    static uint32_t u32(const uint8_t *p) {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
               ((uint32_t)p[3] << 24);
    }
    static uint16_t u16(const uint8_t *p) {
        return (uint16_t)(p[0] | (p[1] << 8));
    }

    void fail(const char *msg) {
        ok_ = false;
        strncpy(err_, msg, sizeof(err_) - 1);
        err_[sizeof(err_) - 1] = 0;
    }

    double fitScale() const {
        double sx = 240.0 / srcW_;
        double sy = (double)kMapViewH / srcH_;
        return sx < sy ? sx : sy;
    }

    double scale() const {
        static const double z[] = {0.0, 0.5, 1.0, 2.0, 4.0};
        return zoomIdx_ == 0 ? fitScale() : z[zoomIdx_];
    }

    void open() {
        ok_ = false;
        err_[0] = 0;
        hasGeo_ = false;
        cx_ = cy_ = 0;
        zoomIdx_ = 0;
        if (!storage::mounted()) { fail("нет microSD"); return; }
        file_ = storage::fs()->open(path_, FILE_READ);
        if (!file_) { fail("файл не найден"); return; }

        uint8_t hdr[54];
        if (file_.read(hdr, 54) != 54 || hdr[0] != 'B' || hdr[1] != 'M') {
            fail("это не BMP");
            return;
        }
        dataOff_ = u32(&hdr[10]);
        int32_t w = (int32_t)u32(&hdr[18]);
        int32_t h = (int32_t)u32(&hdr[22]);
        uint16_t bpp = u16(&hdr[28]);
        uint32_t comp = u32(&hdr[30]);
        topDown_ = h < 0;
        if (h < 0) h = -h;
        if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || bpp != 24 || comp != 0) {
            fail("нужен 24-бит BMP без сжатия, до 4096 px");
            return;
        }
        srcW_ = w;
        srcH_ = h;
        rowSize_ = ((uint32_t)w * 3 + 3) & ~3u;
        if (rowSize_ > sizeof(g_mapRow)) {
            fail("слишком широкая карта");
            return;
        }

        String geoPath = path_;
        int dot = geoPath.lastIndexOf('.');
        if (dot > 0) geoPath = geoPath.substring(0, dot);
        geoPath += ".geo";
        String geo = storage::readText(geoPath.c_str(), 512);
        if (geo.length()) {
            double a = 0, b = 0, c = 0, d = 0;

            auto val = [&](const char *key) -> double {
                int p = geo.indexOf(key);
                if (p < 0) return NAN;
                p = geo.indexOf('=', p);
                return p < 0 ? NAN : atof(geo.c_str() + p + 1);
            };
            a = val("lat_top");
            b = val("lon_left");
            c = val("lat_bottom");
            d = val("lon_right");
            if (!isnan(a) && !isnan(b) && !isnan(c) && !isnan(d) &&
                a > c && d > b) {
                latTop_ = a;
                lonLeft_ = b;
                latBottom_ = c;
                lonRight_ = d;
                hasGeo_ = true;
            }
        }

        cx_ = srcW_ / 2.0;
        cy_ = srcH_ / 2.0;
        if (hasGeo_ && gps::hasFix()) {
            double px, py;
            if (geoToPixel(gps::fix().lat, gps::fix().lon, px, py)) {
                cx_ = px;
                cy_ = py;
            }
        }
        ok_ = true;
    }

    bool geoToPixel(double lat, double lon, double &px, double &py) const {
        if (!hasGeo_) return false;
        if (lat > latTop_ || lat < latBottom_ || lon < lonLeft_ || lon > lonRight_)
            return false;
        double mt = mercY(latTop_), mb = mercY(latBottom_);
        double my = mercY(lat);
        py = (mt - my) / (mt - mb) * srcH_;
        px = (lon - lonLeft_) / (lonRight_ - lonLeft_) * srcW_;
        return true;
    }

    void pixelToGeo(double px, double py, double &lat, double &lon) const {
        double mt = mercY(latTop_), mb = mercY(latBottom_);
        double my = mt - (py / srcH_) * (mt - mb);
        lat = invMercY(my);
        lon = lonLeft_ + (px / srcW_) * (lonRight_ - lonLeft_);
    }

    void clampCenter() {
        double s = scale();
        double halfW = 120.0 / s, halfH = (kMapViewH / 2.0) / s;
        if (srcW_ * s <= 240.0) cx_ = srcW_ / 2.0;
        else cx_ = std::max(halfW, std::min((double)srcW_ - halfW, cx_));
        if (srcH_ * s <= kMapViewH) cy_ = srcH_ / 2.0;
        else cy_ = std::max(halfH, std::min((double)srcH_ - halfH, cy_));
    }

    void pan(int dx, int dy) {
        double s = scale();
        cx_ += dx / s;
        cy_ += dy / s;
        clampCenter();
    }

    void render() {
        const uint16_t bg = gfx::col::BG;
        double s = scale();
        clampCenter();
        int x0 = (int)floor(cx_ - 120.0 / s);
        int y0 = (int)floor(cy_ - (kMapViewH / 2.0) / s);
        int span = (int)ceil(240.0 / s) + 1;
        int lastSy = -100000;
        int sxStart = 0, sxEnd = -1;

        for (int dy = 0; dy < kMapViewH; dy++) {
            int sy = y0 + (int)floor(dy / s);
            int row = dy & 15;
            bool valid = (sy >= 0 && sy < srcH_);

            if (valid && sy != lastSy) {
                lastSy = sy;
                sxStart = std::max(0, x0);
                sxEnd = std::min(srcW_ - 1, x0 + span);
                if (sxEnd >= sxStart) {
                    int ay = topDown_ ? sy : (srcH_ - 1 - sy);
                    uint32_t pos = dataOff_ + (uint32_t)ay * rowSize_ +
                                   (uint32_t)sxStart * 3;
                    int bytes = (sxEnd - sxStart + 1) * 3;
                    if (bytes > (int)sizeof(g_mapRow)) bytes = sizeof(g_mapRow);
                    file_.seek(pos);
                    int got = file_.read(g_mapRow, bytes);
                    if (got < bytes) valid = false;
                } else {
                    valid = false;
                }
            }

            for (int dx = 0; dx < 240; dx++) {
                uint16_t c = bg;
                if (valid) {
                    int sx = x0 + (int)floor(dx / s);
                    if (sx >= sxStart && sx <= sxEnd && sx < srcW_) {
                        const uint8_t *p = &g_mapRow[(sx - sxStart) * 3];
                        c = gfx::col::rgb(p[2], p[1], p[0]);
                    }
                }
                g_mapBuf[row * 240 + dx] = c;
            }
            if (row == 15)
                display::pushImage(0, UI_CONTENT_Y0 + dy - 15, 240, 16, g_mapBuf);
            else if (dy == kMapViewH - 1)
                display::pushImage(0, UI_CONTENT_Y0 + dy - row, 240, row + 1,
                                   g_mapBuf);
        }
    }

    void overlays() {
        const int cx = 120, cy = UI_CONTENT_Y0 + kMapViewH / 2;
        double s = scale();

        gfx::hline(cx - 11, cy, 7, gfx::col::accent);
        gfx::hline(cx + 4, cy, 7, gfx::col::accent);
        gfx::vline(cx, cy - 11, 7, gfx::col::accent);
        gfx::vline(cx, cy + 4, 7, gfx::col::accent);
        gfx::circle(cx, cy, 2, gfx::col::accent);

        for (size_t i = 0; i < wps_.size(); i++) {
            const Wp &p = wps_[i];
            int x = cx + (int)lround((p.x - cx_) * s);
            int y = cy + (int)lround((p.y - cy_) * s);
            if (x < 3 || x > 236 || y < UI_CONTENT_Y0 + 3 || y > UI_CONTENT_Y1 - 20)
                continue;
            gfx::fillCircle(x, y, 4, gfx::col::accent);
            gfx::circle(x, y, 6, gfx::col::WARN);
            char n[8];
            snprintf(n, sizeof(n), "%d", (int)i + 1);
            gfx::text5(x + 6, y - 5, n, gfx::col::TEXT, gfx::col::BLACK);
        }

        if (hasGeo_ && gps::hasFix()) {
            double px, py;
            if (geoToPixel(gps::fix().lat, gps::fix().lon, px, py)) {
                int x = cx + (int)lround((px - cx_) * s);
                int y = cy + (int)lround((py - cy_) * s);
                if (x >= 3 && x < 237 && y >= UI_CONTENT_Y0 + 3 &&
                    y < UI_CONTENT_Y1 - 20) {
                    gfx::fillCircle(x, y, 5, gfx::col::BLUE);
                    gfx::circle(x, y, 7, gfx::col::TEXT);
                    gfx::fillCircle(x, y, 2, gfx::col::BLACK);
                }
            }
        }

        int iy = UI_CONTENT_Y1 - 16;
        gfx::fillRect(0, iy, 240, 16, gfx::col::BG2);
        gfx::hline(0, iy, 240, gfx::col::LINE);
        char left[64];
        if (hasGeo_) {
            double lat, lon;
            pixelToGeo(cx_, cy_, lat, lon);
            char la[20], lo[20];
            snprintf(la, sizeof(la), "%.4f", lat);
            snprintf(lo, sizeof(lo), "%.4f", lon);
            snprintf(left, sizeof(left), "%s %s  x%.2f  %dт", la, lo, s,
                     (int)wps_.size());
        } else {
            snprintf(left, sizeof(left), "x%.0f y%.0f  x%.2f  %dт", cx_, cy_, s,
                     (int)wps_.size());
        }
        gfx::text(5, iy + 12, left, gfx::F_SMALL, gfx::col::TEXT2,
                  gfx::col::BG2);
        if (hasGeo_)
            gfx::textRight(236, iy + 12, "© OSM", gfx::F_SMALL,
                           gfx::col::DIM2, gfx::col::BG2);
    }

    void loadWaypoints() {
        wps_.clear();
        String txt = storage::readText(MAPS_DIR "/waypoints.csv", 20000);
        int p = 0;
        while (p < (int)txt.length() && (int)wps_.size() < 160) {
            int e = txt.indexOf('\n', p);
            if (e < 0) e = txt.length();
            String line = txt.substring(p, e);
            p = e + 1;
            line.trim();
            if (!line.length() || line[0] == '#') continue;
            int f[6], pos = 0;
            for (int i = 0; i < 6; i++) {
                f[i] = line.indexOf(';', pos);
                if (f[i] < 0) { f[i] = line.length(); break; }
                pos = f[i] + 1;
            }
            String map = line.substring(0, f[0]);
            if (map != name_) continue;
            Wp w;
            w.x = line.substring(f[0] + 1, f[1]).toFloat();
            w.y = line.substring(f[1] + 1, f[2]).toFloat();
            w.lat = line.substring(f[2] + 1, f[3]).toFloat();
            w.lon = line.substring(f[3] + 1, f[4]).toFloat();
            w.geo = w.lat != 0 || w.lon != 0;
            wps_.push_back(w);
        }
    }

    void addWaypoint() {
        if (!storage::mounted()) { app::toast("нет microSD"); return; }
        Wp w;
        w.x = cx_;
        w.y = cy_;
        if (hasGeo_) {
            pixelToGeo(cx_, cy_, w.lat, w.lon);
            w.geo = true;
        }
        storage::ensureDir(MAPS_DIR);
        String line;
        line.reserve(96);
        line += name_;
        line += ";";
        line += String((int)lround(w.x));
        line += ";";
        line += String((int)lround(w.y));
        line += ";";
        char b[48];
        snprintf(b, sizeof(b), "%.7f;%.7f;%lu", w.lat, w.lon,
                 (unsigned long)time(nullptr));
        line += b;
        storage::appendLine(MAPS_DIR "/waypoints.csv", line.c_str());
        wps_.push_back(w);
        buzzer::ok();
        if (w.geo) {
            char t[56];
            snprintf(t, sizeof(t), "точка %.4f, %.4f", w.lat, w.lon);
            app::toast("%s", t);
        } else {
            app::toast("точка %d, %d", (int)w.x, (int)w.y);
        }
        klog::add("map point: %s %.5f %.5f", name_.c_str(), w.lat, w.lon);
    }
};

class MapList : public Screen {
public:
    const char *title() const override { return "Карты SD"; }
    const char *hint() const override { return "< back   OK open   ^v move"; }

    void enter() override { reload(); }

    void draw() override {
        const uint16_t bg = gfx::col::BG;
        if (!storage::mounted()) {
            gfx::text(12, UI_CONTENT_Y0 + 30, "Нет карты microSD", gfx::F_BODY_B,
                      gfx::col::WARN, bg);
            return;
        }
        if (files_.empty()) {
            gfx::text(12, UI_CONTENT_Y0 + 30, "Нет карт в /maps", gfx::F_BODY_B,
                      gfx::col::DIM, bg);
            drawWrapped("Скопируйте 24-битный .bmp в /maps.", 12, UI_CONTENT_Y0 + 41, 216,
                        16, gfx::col::DIM, bg, gfx::F_SMALL);
            drawWrapped("Размер 320x240 или 640x480.", 12, UI_CONTENT_Y0 + 73, 216, 16,
                        gfx::col::DIM, bg, gfx::F_SMALL);
            return;
        }
        const int rowH = 30, visible = 6;
        if (cursor_ < scroll_) scroll_ = cursor_;
        if (cursor_ >= scroll_ + visible) scroll_ = cursor_ - visible + 1;
        for (int i = 0; i < visible; i++) {
            int idx = scroll_ + i;
            if (idx >= (int)files_.size()) break;
            int y = UI_CONTENT_Y0 + 2 + i * rowH;
            bool sel = idx == cursor_;
            if (sel) gfx::fillRoundRect(4, y, 232, rowH - 2, 5, gfx::col::PANEL2);
            gfx::drawIcon(gfx::Icon::Map, 12, y + 6, 18,
                          sel ? gfx::col::accent : gfx::col::DIM);
            gfx::text(38, y + 18, files_[idx].c_str(), gfx::F_SMALL,
                      gfx::col::TEXT, sel ? gfx::col::PANEL2 : bg);
        }
    }

    bool onKey(Key k, KeyEvent e) override {
        if (files_.empty()) return false;
        if (k == Key::Up && (e == KeyEvent::Press || e == KeyEvent::Repeat)) {
            cursor_ = (cursor_ - 1 + (int)files_.size()) % (int)files_.size();
            return true;
        }
        if (k == Key::Down && (e == KeyEvent::Press || e == KeyEvent::Repeat)) {
            cursor_ = (cursor_ + 1) % (int)files_.size();
            return true;
        }
        if (k == Key::Ok && e == KeyEvent::Press) {
            app::push(new MapView(String(MAPS_DIR "/") + files_[cursor_]));
            return true;
        }
        return false;
    }

private:
    std::vector<String> files_;
    int cursor_ = 0, scroll_ = 0;

    void reload() {
        files_.clear();
        std::vector<storage::Entry> all;
        storage::list(MAPS_DIR, all);
        for (auto &e : all) {
            String n = e.name;
            n.toLowerCase();
            if (!e.dir && n.endsWith(".bmp")) files_.push_back(e.name);
        }
        if (cursor_ >= (int)files_.size())
            cursor_ = max(0, (int)files_.size() - 1);
        app::invalidate();
    }
};

}

namespace screens {

void openGps() {
    std::vector<MenuItem> items;
    items.push_back({"Данные в реальном времени", gfx::Icon::Gps, "", true,
                     [] { app::push(new GpsLive()); }});
    items.push_back({"Качество привязки", gfx::Icon::Radar, "", true,
                     [] { app::push(new GpsSky()); }});
    items.push_back({"Запись трека", gfx::Icon::Map, "", true,
                     [] { app::push(new GpsTrack()); }});
    items.push_back({"Карта Минска (офлайн)", gfx::Icon::Map, "", true, [] {
                         if (!storage::mounted() ||
                             !storage::exists(MAPS_DIR "/minsk.bmp")) {
                             app::message(
                                 "Карта не найдена",
                                 "Скопируйте папку sd/maps на microSD:\n"
                                 "/maps/minsk.bmp\n"
                                 "/maps/minsk.geo\n\n"
                                 "Готовые файлы лежат в папке sd/maps проекта.");
                             return;
                         }
                         app::push(new MapView(MAPS_DIR "/minsk.bmp"));
                     }});
    items.push_back({"Просмотрщик карт SD", gfx::Icon::Folder, "", true,
                     [] { app::push(new MapList()); }});
    items.push_back({"Установить время", gfx::Icon::Clock, "", true, [] {
                         if (!gps::hasFix()) {
                             app::toast("no GPS fix yet");
                             return;
                         }
                         gps::syncClock(settings.tz);
                         buzzer::ok();
                         app::toast("clock set from GPS");
                     }});
    app::push(new ListScreen("GPS / Map", std::move(items),
                             "< back   OK open   ^v move"));
}

}
