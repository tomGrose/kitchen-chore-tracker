#include "app/date_util.h"

// Algorithms from Howard Hinnant's public-domain "chrono-compatible date
// algorithms" (days_from_civil / civil_from_days / weekday_from_days).

namespace dateutil {

int32_t daysFromCivil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

void civilFromDays(int32_t days, int &year, unsigned &month, unsigned &day)
{
    const int32_t z = days + 719468;
    const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    day = doy - (153 * mp + 2) / 5 + 1;
    month = mp < 10 ? mp + 3 : mp - 9;
    year = static_cast<int>(yoe) + era * 400 + (month <= 2 ? 1 : 0);
}

int weekday(int32_t days)
{
    return static_cast<int>(days >= -4 ? (days + 4) % 7 : (days + 5) % 7 + 6);
}

int daysInMonth(int year, unsigned month)
{
    static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if(month == 2) {
        const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return kDays[month - 1];
}

DayTime addMinutes(const DayTime &t, int minutes)
{
    // Work in absolute minutes so negative deltas borrow days correctly;
    // C++ integer division truncates toward zero, hence the explicit floor.
    const int64_t total = static_cast<int64_t>(t.day) * 1440 + t.minute + minutes;
    int64_t days = total / 1440;
    int64_t rem = total % 1440;
    if(rem < 0) { rem += 1440; --days; }

    DayTime out;
    out.day = static_cast<int32_t>(days);
    out.minute = static_cast<int16_t>(rem);
    return out;
}

bool isBefore(const DayTime &a, const DayTime &b)
{
    if(a.day != b.day) return a.day < b.day;
    return a.minute < b.minute;
}

}  // namespace dateutil
