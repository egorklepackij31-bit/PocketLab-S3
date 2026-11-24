#pragma once

#include <Arduino.h>
#include <FS.h>
#include <vector>

namespace storage {

struct Entry {
    String  name;
    bool    dir;
    size_t  size;
};

bool      begin();
bool      mounted();
fs::FS   *fs();

bool      exists(const char *path);
bool      ensureDir(const char *path);
size_t    fileSize(const char *path);
bool      remove(const char *path);

size_t    appendLine(const char *path, const char *line);
size_t    writeText(const char *path, const char *text);
String    readText(const char *path, size_t maxLen = 8192);

bool      list(const char *path, std::vector<Entry> &out);

uint64_t  totalBytes();
uint64_t  usedBytes();
String    cardType();

}
