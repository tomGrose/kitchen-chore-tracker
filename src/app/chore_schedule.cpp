#include "app/chore_schedule.h"

namespace {

constexpr int32_t LOOKAHEAD_DAYS = 800;

const char *const kWeekdayShort[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

bool everyN(const Schedule &s, int32_t day, int32_t step)
{
    if(s.anchorDay == dateutil::NO_DAY || step <= 0) return false;
    if(day < s.anchorDay) return false;
    return (day - s.anchorDay) % step == 0;
}

// First anchor + k*step on or after `from` (k >= 0).
int32_t nextEveryN(const Schedule &s, int32_t from, int32_t step)
{
    if(s.anchorDay == dateutil::NO_DAY || step <= 0) return dateutil::NO_DAY;
    if(from <= s.anchorDay) return s.anchorDay;
    const int32_t k = (from - s.anchorDay + step - 1) / step;
    return s.anchorDay + k * step;
}

std::string ordinal(int n)
{
    const int mod100 = n % 100;
    const char *suffix = "th";
    if(mod100 < 11 || mod100 > 13) {
        switch(n % 10) {
        case 1: suffix = "st"; break;
        case 2: suffix = "nd"; break;
        case 3: suffix = "rd"; break;
        default: break;
        }
    }
    return std::to_string(n) + suffix;
}

}  // namespace

const char *weekdayShortName(int weekday)
{
    if(weekday < 0 || weekday > 6) return "?";
    return kWeekdayShort[weekday];
}

bool isOccurrence(const Schedule &s, int32_t day)
{
    switch(s.type) {
    case ScheduleType::Daily:
        return true;
    case ScheduleType::Weekdays:
        return ((s.weekdayMask >> dateutil::weekday(day)) & 1) != 0;
    case ScheduleType::EveryNDays:
        return everyN(s, day, s.interval);
    case ScheduleType::EveryNWeeks:
        return everyN(s, day, 7 * static_cast<int32_t>(s.interval));
    case ScheduleType::Monthly: {
        int year = 0;
        unsigned month = 0;
        unsigned dom = 0;
        dateutil::civilFromDays(day, year, month, dom);
        const int length = dateutil::daysInMonth(year, month);
        const int target = s.monthDay > length ? length : s.monthDay;
        return static_cast<int>(dom) == target;
    }
    }
    return false;
}

int32_t nextOccurrenceOnOrAfter(const Schedule &s, int32_t from)
{
    if(s.type == ScheduleType::EveryNDays) return nextEveryN(s, from, s.interval);
    if(s.type == ScheduleType::EveryNWeeks) return nextEveryN(s, from, 7 * static_cast<int32_t>(s.interval));

    for(int32_t offset = 0; offset < LOOKAHEAD_DAYS; ++offset) {
        if(isOccurrence(s, from + offset)) return from + offset;
    }
    return dateutil::NO_DAY;
}

std::string describeSchedule(const Schedule &s)
{
    switch(s.type) {
    case ScheduleType::Daily:
        return "Every day";
    case ScheduleType::Weekdays: {
        int count = 0;
        for(int d = 0; d < 7; ++d) count += (s.weekdayMask >> d) & 1;
        if(count == 0) return "Never";
        if(count == 7) return "Every day";
        std::string text = (count == 1) ? "Weekly on " : "Every ";
        bool first = true;
        for(int d = 0; d < 7; ++d) {
            if(((s.weekdayMask >> d) & 1) == 0) continue;
            if(!first) text += ", ";
            text += kWeekdayShort[d];
            first = false;
        }
        return text;
    }
    case ScheduleType::EveryNDays:
        if(s.interval <= 1) return "Every day";
        return "Every " + std::to_string(s.interval) + " days";
    case ScheduleType::EveryNWeeks: {
        const char *day = (s.anchorDay == dateutil::NO_DAY) ? "?" : kWeekdayShort[dateutil::weekday(s.anchorDay)];
        if(s.interval <= 1) return std::string("Weekly on ") + day;
        return "Every " + std::to_string(s.interval) + " weeks on " + day;
    }
    case ScheduleType::Monthly:
        return "Monthly on the " + ordinal(s.monthDay);
    }
    return "";
}

const char *skipLabel(const Schedule &schedule)
{
    switch(schedule.type) {
    case ScheduleType::Daily:
        return "Skip today";
    case ScheduleType::Weekdays: {
        // One weekday a week reads as "this week"; several means the next one.
        int days = 0;
        for(int bit = 0; bit < 7; ++bit) if(schedule.weekdayMask & (1u << bit)) ++days;
        return days == 1 ? "Skip this week" : "Skip this one";
    }
    case ScheduleType::EveryNWeeks:
        return "Skip this week";
    case ScheduleType::Monthly:
        return "Skip this month";
    case ScheduleType::EveryNDays:
        break;
    }
    return "Skip this one";
}
