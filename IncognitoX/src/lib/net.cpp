#include "lib/net.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "app/log.h"
#include "hw/storage.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

namespace net {
namespace {

State    s_state = State::Off;
String   s_ssid, s_pass;
String   s_err;
uint32_t s_startMs = 0;
bool     s_credsLoaded = false;

constexpr uint32_t kConnectTimeoutMs = 20000;
constexpr size_t   kMaxBody = 90000;

void setErr(const char *e) { s_err = e ? e : ""; }

void loadCreds() {
    if (s_credsLoaded) return;
    s_credsLoaded = true;
    s_ssid = "";
    s_pass = "";

    if (storage::mounted() && storage::exists("/wifi.txt")) {
        String txt = storage::readText("/wifi.txt", 1024);
        int p = 0;
        while (p < (int)txt.length()) {
            int e = txt.indexOf('\n', p);
            if (e < 0) e = txt.length();
            String line = txt.substring(p, e);
            p = e + 1;
            line.trim();
            if (!line.length() || line[0] == '#' || line[0] == ';') continue;
            int eq = line.indexOf('=');
            String k = eq > 0 ? line.substring(0, eq) : line;
            String v = eq > 0 ? line.substring(eq + 1) : String();
            k.trim();
            v.trim();
            if (k.equalsIgnoreCase("ssid")) s_ssid = v;
            else if (k.equalsIgnoreCase("pass") || k.equalsIgnoreCase("password"))
                s_pass = v;
        }
    }

    if (!s_ssid.length()) s_ssid = WIFI_SSID;
    if (!s_pass.length()) s_pass = WIFI_PASS;
}

struct Url {
    bool    tls = true;
    String  host;
    uint16_t port = 443;
    String  path = "/";
};

bool parseUrl(const String &url, Url &u) {
    int sep = url.indexOf("://");
    if (sep <= 0) return false;
    String scheme = url.substring(0, sep);
    scheme.toLowerCase();
    u.tls = (scheme == "https");
    u.port = u.tls ? 443 : 80;
    int start = sep + 3;
    int slash = url.indexOf('/', start);
    String hostPort = slash < 0 ? url.substring(start) : url.substring(start, slash);
    u.path = slash < 0 ? String("/") : url.substring(slash);
    int colon = hostPort.lastIndexOf(':');
    if (colon > 0) {
        u.host = hostPort.substring(0, colon);
        long p = hostPort.substring(colon + 1).toInt();
        if (p > 0 && p < 65536) u.port = (uint16_t)p;
    } else {
        u.host = hostPort;
    }
    return u.host.length() > 0;
}

String joinUrl(const Url &base, const String &next) {
    if (next.startsWith("http://") || next.startsWith("https://")) return next;
    String s = base.tls ? "https://" : "http://";
    s += base.host;
    if ((base.tls && base.port != 443) || (!base.tls && base.port != 80)) {
        s += ":";
        s += String(base.port);
    }
    if (!next.startsWith("/")) s += "/";
    s += next;
    return s;
}

bool readExact(Client &c, size_t n, String &out) {
    uint8_t buf[256];
    size_t got = 0;
    uint32_t last = millis();
    while (got < n && out.length() < kMaxBody) {
        int av = c.available();
        if (av > 0) {
            size_t want = n - got;
            if (want > sizeof(buf)) want = sizeof(buf);
            int r = c.read(buf, want);
            if (r > 0) {
                out.concat((const char *)buf, (size_t)r);
                got += (size_t)r;
                last = millis();
                continue;
            }
        }
        if (!c.connected() && c.available() == 0) break;
        if (millis() - last > 6000) break;
        delay(4);
    }
    return got == n;
}

bool readChunked(Client &c, String &out) {
    while (out.length() < kMaxBody) {
        String line = c.readStringUntil('\n');
        line.trim();
        if (!line.length()) continue;
        int semi = line.indexOf(';');
        if (semi >= 0) line = line.substring(0, semi);
        long n = strtol(line.c_str(), nullptr, 16);
        if (n <= 0) return true;
        if (!readExact(c, (size_t)n, out)) return false;

        c.readStringUntil('\n');
    }
    return true;
}

bool httpGetOnce(const String &url, String &body, uint32_t timeoutMs, int depth) {
    Url u;
    if (!parseUrl(url, u)) { setErr("bad URL"); return false; }

    WiFiClient plain;
    WiFiClientSecure tls;
    Client *c = nullptr;
    if (u.tls) {
        tls.setInsecure();
        tls.setTimeout(12);
        if (!tls.connect(u.host.c_str(), u.port, (int32_t)timeoutMs)) {
            setErr("TLS connect failed");
            return false;
        }
        c = &tls;
    } else {
        plain.setTimeout(12);
        if (!plain.connect(u.host.c_str(), u.port, (int32_t)timeoutMs)) {
            setErr("TCP connect failed");
            return false;
        }
        c = &plain;
    }

    String req;
    req.reserve(220);
    req += "GET ";
    req += u.path;
    req += " HTTP/1.1\r\nHost: ";
    req += u.host;
    if ((u.tls && u.port != 443) || (!u.tls && u.port != 80)) {
        req += ":";
        req += String(u.port);
    }
    req += "\r\nUser-Agent: IncognitoX/1.0\r\nAccept: application/json,*/*\r\n"
           "Connection: close\r\n\r\n";
    c->print(req);

    String status = c->readStringUntil('\n');
    int sp = status.indexOf(' ');
    int code = sp > 0 ? status.substring(sp + 1).toInt() : 0;
    long len = -1;
    bool chunked = false;
    String location;

    while (c->connected() || c->available()) {
        String line = c->readStringUntil('\n');
        if (!line.length() || line == "\r") break;
        line.trim();
        int colon = line.indexOf(':');
        if (colon <= 0) continue;
        String k = line.substring(0, colon);
        String v = line.substring(colon + 1);
        k.toLowerCase();
        v.trim();
        if (k == "content-length") len = v.toInt();
        else if (k == "transfer-encoding" && v.indexOf("chunked") >= 0) chunked = true;
        else if (k == "location") location = v;
    }

    if (code >= 300 && code < 400 && location.length() && depth < 2) {
        String next = joinUrl(u, location);
        c->stop();
        return httpGetOnce(next, body, timeoutMs, depth + 1);
    }
    if (code < 200 || code >= 300) {
        s_err = "HTTP ";
        s_err += String(code);
        c->stop();
        return false;
    }

    body = "";
    body.reserve(min((size_t)(len > 0 ? len : 4096), (size_t)16384));
    bool ok = true;
    if (chunked) {
        ok = readChunked(*c, body);
    } else if (len >= 0) {
        ok = readExact(*c, (size_t)len, body);
    } else {
        uint8_t buf[512];
        uint32_t last = millis();
        while (body.length() < kMaxBody) {
            int av = c->available();
            if (av > 0) {
                int r = c->read(buf, min((size_t)av, (size_t)sizeof(buf)));
                if (r > 0) {
                    body.concat((const char *)buf, (size_t)r);
                    last = millis();
                    continue;
                }
            }
            if (!c->connected() && c->available() == 0) break;
            if (millis() - last > 6000) break;
            delay(4);
        }
    }
    c->stop();
    if (!ok && body.length() == 0) {
        setErr("short read");
        return false;
    }
    return true;
}

int findValue(const String &src, const char *key) {
    String pat = "\"";
    pat += key;
    pat += "\"";
    int p = src.indexOf(pat);
    if (p < 0) return -1;
    p += pat.length();
    while (p < (int)src.length()) {
        char c = src[p];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { p++; continue; }
        break;
    }
    if (p >= (int)src.length() || src[p] != ':') return -1;
    p++;
    while (p < (int)src.length()) {
        char c = src[p];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { p++; continue; }
        break;
    }
    return p;
}

}

void begin() {
    s_credsLoaded = false;
    s_state = State::Off;
    s_err = "";
    loadCreds();

    if (storage::mounted() && !storage::exists("/wifi.txt")) {
        storage::writeText("/wifi.txt",
                           "# IncognitoX: укажите свою сеть и сохраните файл\r\n"
                           "ssid=\r\n"
                           "pass=\r\n");
    }
}

bool hasCreds() { loadCreds(); return s_ssid.length() > 0; }

void start() {
    loadCreds();
    if (!s_ssid.length()) {
        s_state = State::NoCreds;
        setErr("no Wi-Fi credentials");
        return;
    }
    if (WiFi.status() == WL_CONNECTED) {
        s_state = State::Online;
        return;
    }
    s_err = "";
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(s_ssid.c_str(), s_pass.c_str());
    s_state = State::Connecting;
    s_startMs = millis();
    klog::add("wifi: connecting to %s", s_ssid.c_str());
}

void tick() {
    if (WiFi.status() == WL_CONNECTED) {
        if (s_state != State::Online) {
            s_state = State::Online;
            s_err = "";
            klog::add("wifi: online %s", WiFi.localIP().toString().c_str());
        }
        return;
    }
    if (s_state == State::Connecting) {
        if (millis() - s_startMs > kConnectTimeoutMs) {
            s_state = State::Failed;
            setErr("connection timeout");
        }
    }
}

bool ensure(uint32_t timeoutMs) {
    loadCreds();
    if (!s_ssid.length()) { s_state = State::NoCreds; return false; }
    if (WiFi.status() == WL_CONNECTED) { s_state = State::Online; return true; }
    if (s_state != State::Connecting) start();

    uint32_t t0 = millis();
    while (millis() - t0 < timeoutMs) {
        if (WiFi.status() == WL_CONNECTED) {
            s_state = State::Online;
            s_err = "";
            return true;
        }
        delay(80);
    }
    s_state = State::Failed;
    if (WiFi.status() == WL_NO_SSID_AVAIL) setErr("network not found");
    else if (WiFi.status() == WL_CONNECT_FAILED) setErr("wrong password?");
    else setErr("connection timeout");
    return false;
}

void off() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    s_state = State::Off;
    s_err = "";
}

State state() { return s_state; }
bool  online() { return WiFi.status() == WL_CONNECTED; }

const char *stateText() {
    switch (s_state) {
    case State::Off: return "выключен";
    case State::NoCreds: return "нет данных";
    case State::Connecting: return "подключение...";
    case State::Online: return "онлайн";
    case State::Failed: return "ошибка";
    }
    return "?";
}

String ssid() { loadCreds(); return s_ssid; }
String ip() { return WiFi.localIP().toString(); }
int    rssi() { return WiFi.RSSI(); }
String errorText() { return s_err; }

bool httpGet(const String &url, String &body, uint32_t timeoutMs) {
    body = "";
    s_err = "";
    if (!online()) { setErr("no Wi-Fi"); return false; }
    bool ok = httpGetOnce(url, body, timeoutMs, 0);
    if (!ok) klog::add("http: %s -> %s", url.c_str(), s_err.c_str());
    return ok;
}

bool jsonNumber(const String &src, const char *key, double &out) {
    int p = findValue(src, key);
    if (p < 0) return false;
    bool quoted = (p < (int)src.length() && src[p] == '"');
    if (quoted) p++;
    const char *s = src.c_str() + p;
    char *end = nullptr;
    double v = strtod(s, &end);
    if (end == s) return false;
    out = v;
    return true;
}

bool jsonInt(const String &src, const char *key, int &out) {
    double v = 0;
    if (!jsonNumber(src, key, v)) return false;
    out = (int)v;
    return true;
}

String jsonString(const String &src, const char *key) {
    int p = findValue(src, key);
    if (p < 0 || p >= (int)src.length() || src[p] != '"') return String();
    p++;
    String out;
    while (p < (int)src.length()) {
        char c = src[p++];
        if (c == '"') break;
        if (c == '\\' && p < (int)src.length()) {
            char e = src[p++];
            switch (e) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            default: out += e; break;
            }
        } else {
            out += c;
        }
    }
    return out;
}

}
