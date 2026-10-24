#pragma once
/*---------------------------------------------------------------
 * Calendar math on plain day numbers (days since 1970-01-01).
 * No Arduino or time-zone dependencies, so it also builds on a PC.
 * "Local" days are used throughout: the clock layer converts to the
 * household time zone before anything reaches the chore engine.
 *--------------------------------------------------------------*/
#include <stdint.h>

namespace dateutil {

constexpr int32_t NO_DAY = INT32_MIN;   // "no date" marker

struct DayTime {
    int32_t day = 0;      // days since 1970-01-01, local civil date
    int16_t minute = 0;   // minutes since local midnight, 0..1439
};

int32_t daysFromCivil(int year, unsigned month, unsigned day);
void    civilFromDays(int32_t days, int &year, unsigned &month, unsigned &day);
int     weekday(int32_t days);                  // 0 = Sunday ... 6 = Saturday
int     daysInMonth(int year, unsigned month);  // month is 1..12

}  // namespace dateutil
