#pragma once
#include <stdint.h>

#include <string>

#include "app/chore_types.h"

// True if the schedule has an occurrence on this day.
bool isOccurrence(const Schedule &schedule, int32_t day);

// First occurrence on or after `from`, or dateutil::NO_DAY if none exists
// within about two years.
int32_t nextOccurrenceOnOrAfter(const Schedule &schedule, int32_t from);

// Human-readable description, e.g. "Weekly on Mon", "Every 2 weeks on Mon".
std::string describeSchedule(const Schedule &schedule);

// Short weekday name for 0 = Sunday ... 6 = Saturday.
const char *weekdayShortName(int weekday);
