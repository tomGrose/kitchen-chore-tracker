#pragma once
/*---------------------------------------------------------------
 * Chore data model (spec section 4).
 * Status is never stored; it is computed from these fields by
 * ChoreEngine::evaluate().
 *--------------------------------------------------------------*/
#include <stdint.h>

#include <string>
#include <vector>

#include "app/date_util.h"

enum class ScheduleType : uint8_t {
    Daily,         // every day
    Weekdays,      // chosen weekdays (one weekday = "weekly on X")
    EveryNDays,    // anchorDay + k * interval days
    EveryNWeeks,   // anchorDay + k * interval weeks (anchor sets the weekday)
    Monthly        // day of month (clamped to the month's length)
};

struct Schedule {
    ScheduleType type = ScheduleType::Daily;
    uint8_t  weekdayMask = 0;                 // bit 0 = Sunday ... bit 6 = Saturday
    uint16_t interval = 1;                    // N for EveryNDays / EveryNWeeks
    int32_t  anchorDay = dateutil::NO_DAY;    // first occurrence for the every-N types
    uint8_t  monthDay = 1;                    // 1..31 for Monthly
};

struct Chore {
    uint16_t    id = 0;
    std::string name;
    std::string icon;                         // optional, unused until the UI has icons
    uint32_t    color = 0;                    // 0xRRGGBB colour tag, 0 = none
    Schedule    schedule;
    int16_t     dueMinute = -1;               // minutes after midnight, -1 = anytime
    int32_t     startDay = 0;                 // occurrences before this day are ignored
    int32_t     lastCompletedDay = dateutil::NO_DAY;
    int16_t     lastCompletedMinute = 0;
    int32_t     coversDay = dateutil::NO_DAY; // occurrence satisfied by an early completion
    std::string completedBy;
    uint16_t    currentStreak = 0;
    uint16_t    bestStreak = 0;
    bool        enabled = true;
};

// One completion, plus the chore's state before it so it can be undone.
struct HistoryEntry {
    uint16_t    choreId = 0;
    int32_t     day = 0;
    int16_t     minute = 0;
    std::string by;
    int32_t     prevCompletedDay = dateutil::NO_DAY;
    int16_t     prevCompletedMinute = 0;
    int32_t     prevCoversDay = dateutil::NO_DAY;
    uint16_t    prevStreak = 0;
    uint16_t    prevBest = 0;
};

struct ChoreData {
    std::vector<Chore>        chores;
    std::vector<HistoryEntry> history;
    uint16_t                  nextId = 1;
};
