#include "app/seed_chores.h"

namespace {

enum Day : uint8_t { SUN = 0, MON, TUE, WED, THU, FRI, SAT };

Chore weekly(uint16_t id, const char *name, Day day, int32_t startDay)
{
    Chore c;
    c.id = id;
    c.name = name;
    c.schedule.type = ScheduleType::Weekdays;
    c.schedule.weekdayMask = static_cast<uint8_t>(1U << day);
    c.startDay = startDay;
    return c;
}

}  // namespace

ChoreData seedChoreData(int32_t startDay)
{
    ChoreData d;

    // Source: chores.md. All chores are "anytime" (dueMinute = -1).
    Chore costco;
    costco.id = 1;
    costco.name = "Costco run";
    costco.schedule.type = ScheduleType::EveryNWeeks;
    costco.schedule.interval = 2;
    costco.schedule.anchorDay = dateutil::daysFromCivil(2026, 10, 26);  // Monday, first due date
    costco.startDay = startDay;
    d.chores.push_back(costco);

    d.chores.push_back(weekly(2, "Mop", MON, startDay));
    d.chores.push_back(weekly(3, "Vacuum (Tue)", TUE, startDay));
    d.chores.push_back(weekly(4, "Grocery store", TUE, startDay));
    d.chores.push_back(weekly(5, "Take out trash", WED, startDay));
    d.chores.push_back(weekly(6, "Vacuum (Thu)", THU, startDay));
    d.chores.push_back(weekly(7, "Clean bathroom", THU, startDay));
    d.chores.push_back(weekly(8, "Water plants", FRI, startDay));

    d.nextId = 9;
    return d;
}
