#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

// Pure validation helpers: no Arduino, storage, network, or local timezone.
namespace TLEValidation {
inline bool timeReady(time_t now) { return now >= (time_t)1704067200; } // 2024-01-01 UTC
inline bool observerValid(bool saved, double lat, double lon) {
    return saved && isfinite(lat) && isfinite(lon) &&
           lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
}
inline bool leapYear(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}
inline bool digits(const char* text, size_t count) {
    for (size_t i = 0; i < count; ++i)
        if (text[i] < '0' || text[i] > '9') return false;
    return true;
}
inline time_t epochFromLine1(const char* line) {
    if (!line || strlen(line) < 32 || line[0] != '1' || line[1] != ' ' ||
        !digits(line + 18, 5) || line[23] != '.' || !digits(line + 24, 8)) return 0;
    int year = (line[18] - '0') * 10 + line[19] - '0';
    year += year >= 57 ? 1900 : 2000;
    char dayText[13] = {};
    memcpy(dayText, line + 20, 12);
    double day = strtod(dayText, nullptr);
    if (day < 1.0 || day >= (leapYear(year) ? 367.0 : 366.0)) return 0;
    int64_t days = 0;
    for (int y = 1970; y < year; ++y) days += leapYear(y) ? 366 : 365;
    for (int y = year; y < 1970; ++y) days -= leapYear(y) ? 366 : 365;
    return (time_t)(days * 86400 + (int64_t)((day - 1.0) * 86400.0));
}
inline bool lineValid(const char* line, char number, uint32_t id) {
    if (!line || strlen(line) != 69 || line[0] != number || line[1] != ' ' ||
        !digits(line + 2, 5) || line[68] < '0' || line[68] > '9') return false;
    char idText[6] = {};
    memcpy(idText, line + 2, 5);
    if ((uint32_t)strtoul(idText, nullptr, 10) != id) return false;
    unsigned checksum = 0;
    for (size_t i = 0; i < 68; ++i) {
        if (line[i] >= '0' && line[i] <= '9') checksum += line[i] - '0';
        else if (line[i] == '-') ++checksum;
    }
    return checksum % 10 == (unsigned)(line[68] - '0');
}
inline bool pairValid(uint32_t id, const char* line1, const char* line2) {
    return id > 0 && id <= 99999 && lineValid(line1, '1', id) &&
           lineValid(line2, '2', id) && epochFromLine1(line1) != 0;
}
}
