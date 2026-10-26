#include "time/app_clock.h"

#include <Arduino.h>

#include "app_log.h"
#include "config.h"

namespace {

constexpr const char *TAG = "clock";

int s_offset_days = 0;
bool s_was_valid = false;

}  // namespace

namespace appclock {

void init(const std::string &timezonePosix, const std::string &ntpServer, int offsetDays)
{
    s_offset_days = offsetDays;

    // configTzTime applies the POSIX TZ rules and starts SNTP. The ESP-IDF
    // re-syncs on its own interval afterwards, so there is nothing to poll.
    configTzTime(timezonePosix.c_str(), ntpServer.c_str());
    LOGI(TAG, "SNTP started: server %s, tz %s", ntpServer.c_str(), timezonePosix.c_str());
}

bool isValid()
{
    const bool valid = time(nullptr) > static_cast<time_t>(cfg::TIME_VALID_EPOCH);
    if(valid && !s_was_valid) {
        s_was_valid = true;
        char stamp[32];
        const time_t t = time(nullptr);
        struct tm local {};
        localtime_r(&t, &local);
        strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
        LOGI(TAG, "time synced: %s local", stamp);
    }
    return valid;
}

dateutil::DayTime now()
{
    const time_t t = time(nullptr);
    struct tm local {};
    localtime_r(&t, &local);

    dateutil::DayTime d;
    d.day = dateutil::daysFromCivil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday) + s_offset_days;
    d.minute = static_cast<int16_t>(local.tm_hour * 60 + local.tm_min);
    return d;
}

time_t epoch()
{
    const time_t t = time(nullptr);
    return t > static_cast<time_t>(cfg::TIME_VALID_EPOCH) ? t : 0;
}

int devOffsetDays()
{
    return s_offset_days;
}

void setDevOffsetDays(int days)
{
    s_offset_days = days;
}

}  // namespace appclock
