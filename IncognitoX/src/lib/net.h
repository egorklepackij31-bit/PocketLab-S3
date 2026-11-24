#pragma once

#include <Arduino.h>

namespace net {

enum class State : uint8_t { Off, NoCreds, Connecting, Online, Failed };

void        begin();
bool        hasCreds();
void        start();
void        tick();
bool        ensure(uint32_t timeoutMs = 15000);
void        off();

State       state();
bool        online();
const char *stateText();
String      ssid();
String      ip();
int         rssi();
String      errorText();

bool        httpGet(const String &url, String &body, uint32_t timeoutMs = 12000);

bool        jsonNumber(const String &src, const char *key, double &out);
bool        jsonInt(const String &src, const char *key, int &out);
String      jsonString(const String &src, const char *key);

}
