#include "hw/storage.h"

#include <SPI.h>
#include <SD.h>

#include "config.h"
#include "pins.h"

namespace storage {
namespace {
bool s_mounted = false;
}

bool begin() {

    s_mounted = SD.begin(PIN_SD_CS, SPI, 20000000);
    if (s_mounted) {
        ensureDir(LOG_DIR);
        ensureDir(WARDIRVE_DIR);
        ensureDir(SUBGHZ_DIR);
        ensureDir(IR_DIR);
        ensureDir(MAPS_DIR);
    }
    return s_mounted;
}

bool mounted() { return s_mounted; }
fs::FS *fs() { return &SD; }

bool exists(const char *path) { return s_mounted && SD.exists(path); }

bool ensureDir(const char *path) {
    if (!s_mounted) return false;
    if (SD.exists(path)) return true;
    return SD.mkdir(path);
}

size_t fileSize(const char *path) {
    if (!s_mounted) return 0;
    File f = SD.open(path, FILE_READ);
    if (!f) return 0;
    size_t s = f.size();
    f.close();
    return s;
}

bool remove(const char *path) {
    if (!s_mounted || !SD.exists(path)) return false;
    return SD.remove(path);
}

size_t appendLine(const char *path, const char *line) {
    if (!s_mounted) return 0;
    File f = SD.open(path, FILE_APPEND);
    if (!f) return 0;
    size_t n = f.print(line);
    n += f.print("\r\n");
    f.close();
    return n;
}

size_t writeText(const char *path, const char *text) {
    if (!s_mounted) return 0;
    File f = SD.open(path, FILE_WRITE);
    if (!f) return 0;
    size_t n = f.print(text);
    f.close();
    return n;
}

String readText(const char *path, size_t maxLen) {
    String out;
    if (!s_mounted) return out;
    File f = SD.open(path, FILE_READ);
    if (!f) return out;
    out.reserve(min((size_t)f.size() + 1, maxLen + 1));
    while (f.available() && out.length() < maxLen) out += (char)f.read();
    f.close();
    return out;
}

bool list(const char *path, std::vector<Entry> &out) {
    out.clear();
    if (!s_mounted) return false;
    File dir = SD.open(path);
    if (!dir) return false;
    if (!dir.isDirectory()) { dir.close(); return false; }

    std::vector<Entry> files;
    File e = dir.openNextFile();
    while (e) {
        Entry en;
        en.name = String(e.name());
        en.dir = e.isDirectory();
        en.size = en.dir ? 0 : e.size();
        if (en.dir) out.push_back(en);
        else files.push_back(en);
        e.close();
        e = dir.openNextFile();
    }
    dir.close();
    for (auto &f : files) out.push_back(f);
    return true;
}

uint64_t totalBytes() {
    if (!s_mounted) return 0;
    return SD.totalBytes();
}

uint64_t usedBytes() {
    if (!s_mounted) return 0;
    return SD.usedBytes();
}

String cardType() {
    if (!s_mounted) return "none";
    switch (SD.cardType()) {
    case CARD_MMC:  return "MMC";
    case CARD_SD:   return "SD";
    case CARD_SDHC: return "SDHC";
    default:        return "unknown";
    }
}

}
