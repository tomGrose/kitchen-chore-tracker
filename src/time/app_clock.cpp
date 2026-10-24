#include "time/app_clock.h"

#include <Arduino.h>
#include <Preferences.h>

#include "config.h"

namespace {

Preferences s_prefs;
int32_t s_base_day = 0;
int s_offset_days = 0;

}  // namespace

namespace appclock {

void init()
{
    s_prefs.begin("clock", false);
    s_offset_days = s_prefs.getInt("devoff", 0);
    s_base_day = dateutil::daysFromCivil(cfg::DEV_START_YEAR, cfg::DEV_START_MONTH, cfg::DEV_START_DAY);
}

dateutil::DayTime now()
{
    const uint32_t seconds = cfg::DEV_START_SECOND_OF_DAY + millis() / 1000;

    dateutil::DayTime t;
    t.day = s_base_day + s_offset_days + static_cast<int32_t>(seconds / 86400);
    t.minute = static_cast<int16_t>((seconds % 86400) / 60);
    return t;
}

int devOffsetDays()
{
    return s_offset_days;
}

void devShiftDays(int delta)
{
    s_offset_days += delta;
    s_prefs.putInt("devoff", s_offset_days);
}

void devReset()
{
    s_offset_days = 0;
    s_prefs.putInt("devoff", 0);
}

}  // namespace appclock
