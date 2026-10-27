// Host-side tests for the chore engine (runs on a PC, not on the board).
//
//   ARDUINOJSON_DIR=<dir containing ArduinoJson.h> tools/host_test/run.sh
//
// app_controller.cpp is excluded there: it is the only file under src/app that
// needs Arduino and LVGL.
//
// The engine has no Arduino dependencies, so this compiles with plain g++.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app/chore_engine.h"
#include "app/chore_json.h"
#include "app/chore_schedule.h"
#include "app/nag_engine.h"
#include "app/seed_chores.h"

using dateutil::DayTime;
using dateutil::NO_DAY;

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++g_checks;                                                                 \
        if(!(cond)) {                                                               \
            ++g_failed;                                                             \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
        }                                                                           \
    } while(0)

static int32_t D(int y, unsigned m, unsigned d) { return dateutil::daysFromCivil(y, m, d); }
static DayTime at(int32_t day, int minute = 12 * 60) { DayTime t; t.day = day; t.minute = static_cast<int16_t>(minute); return t; }

static Status statusOf(const ChoreEngine &e, uint16_t id, const DayTime &now)
{
    return e.evaluate(*e.find(id), now).status;
}

// Chore ids from seed_chores.cpp
enum { COSTCO = 1, MOP, VAC_TUE, GROCERY, TRASH, VAC_THU, BATH, PLANTS };

static void test_dates()
{
    CHECK(D(1970, 1, 1) == 0);
    CHECK(dateutil::weekday(0) == 4);                       // Thursday
    CHECK(dateutil::weekday(D(2026, 10, 22)) == 4);         // Thu Oct 22 2026
    CHECK(dateutil::weekday(D(2026, 10, 26)) == 1);         // Mon Oct 26 2026

    for(int32_t d = D(1999, 1, 1); d < D(2040, 1, 1); d += 7) {
        int y; unsigned m, dom;
        dateutil::civilFromDays(d, y, m, dom);
        CHECK(dateutil::daysFromCivil(y, m, dom) == d);
    }
    CHECK(dateutil::daysInMonth(2028, 2) == 29);
    CHECK(dateutil::daysInMonth(2027, 2) == 28);
    CHECK(dateutil::daysInMonth(2100, 2) == 28);
}

static void test_schedules()
{
    Schedule costco;
    costco.type = ScheduleType::EveryNWeeks;
    costco.interval = 2;
    costco.anchorDay = D(2026, 10, 26);
    CHECK(isOccurrence(costco, D(2026, 10, 26)));
    CHECK(!isOccurrence(costco, D(2026, 11, 2)));
    CHECK(isOccurrence(costco, D(2026, 11, 9)));
    CHECK(!isOccurrence(costco, D(2026, 10, 12)));          // before the anchor
    CHECK(nextOccurrenceOnOrAfter(costco, D(2026, 10, 22)) == D(2026, 10, 26));
    CHECK(nextOccurrenceOnOrAfter(costco, D(2026, 10, 27)) == D(2026, 11, 9));
    CHECK(nextOccurrenceOnOrAfter(costco, D(2026, 11, 9)) == D(2026, 11, 9));
    CHECK(describeSchedule(costco) == "Every 2 weeks on Mon");

    Schedule monthly;
    monthly.type = ScheduleType::Monthly;
    monthly.monthDay = 31;
    CHECK(isOccurrence(monthly, D(2027, 2, 28)));           // clamped to month end
    CHECK(!isOccurrence(monthly, D(2027, 2, 27)));
    CHECK(isOccurrence(monthly, D(2026, 12, 31)));
    CHECK(describeSchedule(monthly) == "Monthly on the 31st");
    monthly.monthDay = 2;
    CHECK(describeSchedule(monthly) == "Monthly on the 2nd");
    monthly.monthDay = 12;
    CHECK(describeSchedule(monthly) == "Monthly on the 12th");

    Schedule tuThu;
    tuThu.type = ScheduleType::Weekdays;
    tuThu.weekdayMask = (1U << 2) | (1U << 4);
    CHECK(describeSchedule(tuThu) == "Every Tue, Thu");
    CHECK(nextOccurrenceOnOrAfter(tuThu, D(2026, 10, 22)) == D(2026, 10, 22));
    CHECK(nextOccurrenceOnOrAfter(tuThu, D(2026, 10, 23)) == D(2026, 10, 27));

    Schedule everyThree;
    everyThree.type = ScheduleType::EveryNDays;
    everyThree.interval = 3;
    everyThree.anchorDay = D(2026, 10, 1);
    CHECK(isOccurrence(everyThree, D(2026, 10, 4)));
    CHECK(!isOccurrence(everyThree, D(2026, 10, 5)));

    Schedule never;
    never.type = ScheduleType::Weekdays;
    never.weekdayMask = 0;
    CHECK(nextOccurrenceOnOrAfter(never, 0) == NO_DAY);
}

static void test_first_boot_statuses()
{
    const int32_t thu = D(2026, 10, 22);
    ChoreEngine e;
    e.data() = seedChoreData(thu);

    const DayTime now = at(thu);
    CHECK(statusOf(e, VAC_THU, now) == Status::Due);
    CHECK(statusOf(e, BATH, now) == Status::Due);
    CHECK(statusOf(e, MOP, now) == Status::Upcoming);       // Monday, not overdue on day one
    CHECK(statusOf(e, COSTCO, now) == Status::Upcoming);
    CHECK(e.evaluate(*e.find(COSTCO), now).nextDay == D(2026, 10, 26));
    CHECK(e.evaluate(*e.find(MOP), now).nextDay == D(2026, 10, 26));
    CHECK(e.remaining(now) == 2);

    // Friday with nothing done: Thursday chores carry over, plants are due.
    const DayTime fri = at(thu + 1);
    CHECK(statusOf(e, VAC_THU, fri) == Status::Overdue);
    CHECK(e.evaluate(*e.find(VAC_THU), fri).daysOverdue == 1);
    CHECK(statusOf(e, PLANTS, fri) == Status::Due);

    // Sorted: overdue first, then due, then upcoming.
    const auto view = e.buildView(fri);
    CHECK(view.size() == 8);
    CHECK(view[0].status == Status::Overdue && view[1].status == Status::Overdue);
    CHECK(view[2].status == Status::Due && view[2].id == PLANTS);
    CHECK(view.back().status == Status::Upcoming);
    CHECK(e.remaining(fri) == 3);

    // Monday: Costco is due (first anchor day) together with Mop; Thursday chores
    // have been overdue for 4 days.
    const DayTime mon = at(D(2026, 10, 26));
    CHECK(statusOf(e, COSTCO, mon) == Status::Due);
    CHECK(statusOf(e, MOP, mon) == Status::Due);
    CHECK(e.evaluate(*e.find(VAC_THU), mon).daysOverdue == 4);
    CHECK(e.evaluate(*e.find(PLANTS), mon).daysOverdue == 3);
}

static void test_complete_undo_streak()
{
    const int32_t thu = D(2026, 10, 22);
    ChoreEngine e;
    e.data() = seedChoreData(thu);

    // On time: done on Thursday.
    CHECK(e.complete(VAC_THU, at(thu, 18 * 60), "Tom"));
    CHECK(statusOf(e, VAC_THU, at(thu, 19 * 60)) == Status::Done);
    CHECK(e.find(VAC_THU)->currentStreak == 1);
    CHECK(!e.complete(VAC_THU, at(thu, 19 * 60), "Tom"));   // already done today
    CHECK(e.data().history.size() == 1);

    // Next day: upcoming, next Thursday.
    CHECK(statusOf(e, VAC_THU, at(thu + 1)) == Status::Upcoming);
    CHECK(e.evaluate(*e.find(VAC_THU), at(thu + 1)).nextDay == thu + 7);

    // On time again the following Thursday: streak 2.
    CHECK(statusOf(e, VAC_THU, at(thu + 7)) == Status::Due);
    CHECK(e.complete(VAC_THU, at(thu + 7), "Tom"));
    CHECK(e.find(VAC_THU)->currentStreak == 2);
    CHECK(e.find(VAC_THU)->bestStreak == 2);

    // Miss a week, complete 2 days late: streak resets to 1, best stays 2.
    CHECK(statusOf(e, VAC_THU, at(thu + 16)) == Status::Overdue);
    CHECK(e.evaluate(*e.find(VAC_THU), at(thu + 16)).daysOverdue == 2);
    CHECK(e.complete(VAC_THU, at(thu + 16), "Tom"));
    CHECK(e.find(VAC_THU)->currentStreak == 1);
    CHECK(e.find(VAC_THU)->bestStreak == 2);
    CHECK(statusOf(e, VAC_THU, at(thu + 16)) == Status::Done);
    CHECK(statusOf(e, VAC_THU, at(thu + 17)) == Status::Upcoming);

    // Undo restores the previous state exactly.
    CHECK(e.canUndo(VAC_THU));
    CHECK(e.undoLast(VAC_THU));
    CHECK(e.find(VAC_THU)->currentStreak == 2);
    CHECK(e.find(VAC_THU)->lastCompletedDay == thu + 7);
    CHECK(statusOf(e, VAC_THU, at(thu + 16)) == Status::Overdue);

    // Undo all the way back.
    CHECK(e.undoLast(VAC_THU));
    CHECK(e.undoLast(VAC_THU));
    CHECK(!e.canUndo(VAC_THU));
    CHECK(!e.undoLast(VAC_THU));
    CHECK(e.find(VAC_THU)->lastCompletedDay == NO_DAY);
    CHECK(e.find(VAC_THU)->currentStreak == 0);
    CHECK(e.data().history.empty());

    // The displayed streak is 0 while an occurrence is carried over.
    e.complete(BATH, at(thu), "Tom");
    CHECK(e.find(BATH)->currentStreak == 1);
    const DayTime nextWeekLate = at(thu + 9);
    bool found = false;
    for(const ChoreView &v : e.buildView(nextWeekLate)) {
        if(v.id == BATH) { found = true; CHECK(v.status == Status::Overdue); CHECK(v.streak == 0); }
    }
    CHECK(found);

    CHECK(!e.complete(999, at(thu), "Tom"));                // unknown chore
}

static void test_early_completion()
{
    const int32_t thu = D(2026, 10, 22);
    ChoreEngine e;
    e.setEarlyWindowDays(1);
    e.data() = seedChoreData(thu);

    // Water plants are due Friday. Done Thursday (1 day early) covers Friday.
    CHECK(statusOf(e, PLANTS, at(thu)) == Status::Upcoming);
    CHECK(e.complete(PLANTS, at(thu), "Tom"));
    CHECK(e.find(PLANTS)->currentStreak == 1);
    CHECK(statusOf(e, PLANTS, at(thu)) == Status::Done);
    CHECK(statusOf(e, PLANTS, at(thu + 1)) == Status::Done);                 // Friday: covered
    CHECK(statusOf(e, PLANTS, at(thu + 2)) == Status::Upcoming);
    CHECK(e.evaluate(*e.find(PLANTS), at(thu + 2)).nextDay == thu + 8);      // next Friday
    CHECK(e.undoLast(PLANTS));
    CHECK(statusOf(e, PLANTS, at(thu + 1)) == Status::Due);

    // Mop is due Monday (4 days away): outside the window, Monday still due.
    CHECK(e.complete(MOP, at(thu), "Tom"));
    CHECK(e.find(MOP)->currentStreak == 0);
    CHECK(statusOf(e, MOP, at(thu)) == Status::Done);
    CHECK(statusOf(e, MOP, at(thu + 1)) == Status::Upcoming);
    CHECK(statusOf(e, MOP, at(D(2026, 10, 26))) == Status::Due);
}

static void test_due_time_and_daily()
{
    const int32_t thu = D(2026, 10, 22);
    ChoreEngine e;

    Chore pills;
    pills.id = 1;
    pills.name = "Evening chore";
    pills.schedule.type = ScheduleType::Daily;
    pills.dueMinute = 18 * 60;
    pills.startDay = thu;
    e.data().chores.push_back(pills);

    CHECK(statusOf(e, 1, at(thu, 17 * 60 + 59)) == Status::Due);
    CHECK(statusOf(e, 1, at(thu, 18 * 60)) == Status::Overdue);
    CHECK(e.evaluate(*e.find(1), at(thu, 18 * 60)).daysOverdue == 0);

    // Daily chore: a miss carries over; completing late breaks the streak.
    CHECK(e.complete(1, at(thu, 18 * 60 + 30), "Tom"));      // same day, after the time: still on time
    CHECK(e.find(1)->currentStreak == 1);
    CHECK(statusOf(e, 1, at(thu + 1, 8 * 60)) == Status::Due);
    CHECK(e.complete(1, at(thu + 1, 9 * 60), "Tom"));
    CHECK(e.find(1)->currentStreak == 2);
    CHECK(statusOf(e, 1, at(thu + 4, 8 * 60)) == Status::Overdue);
    CHECK(e.evaluate(*e.find(1), at(thu + 4, 8 * 60)).daysOverdue == 2);   // missed since thu+2
    CHECK(e.complete(1, at(thu + 4, 9 * 60), "Tom"));
    CHECK(e.find(1)->currentStreak == 1);

    // A disabled chore is hidden and cannot be completed.
    e.data().chores[0].enabled = false;
    CHECK(e.buildView(at(thu + 5)).empty());
    CHECK(!e.complete(1, at(thu + 5), "Tom"));
    CHECK(e.remaining(at(thu + 5)) == 0);
}

static void test_history_prune()
{
    ChoreEngine e;
    for(int i = 0; i < 5; ++i) {
        HistoryEntry h;
        h.choreId = 1;
        h.day = 1000 + i * 40;
        e.data().history.push_back(h);
    }
    e.pruneHistory(1160, 90);                                // cutoff 1070
    CHECK(e.data().history.size() == 3);
    CHECK(e.data().history.front().day == 1080);
}

static void test_json()
{
    const int32_t thu = D(2026, 10, 22);
    ChoreEngine e;
    e.data() = seedChoreData(thu);
    e.data().chores[0].icon = "cart";
    e.data().chores[0].color = 0xAA5500;
    e.complete(VAC_THU, at(thu, 18 * 60), "Tom");
    e.complete(PLANTS, at(thu, 9 * 60), "Tom");              // early, covers Friday

    const std::string json = chorejson::serialize(e.data());
    ChoreData back;
    CHECK(chorejson::parse(json.c_str(), json.size(), back));
    CHECK(back.chores.size() == e.data().chores.size());
    CHECK(back.history.size() == 2);
    CHECK(back.nextId == 9);

    for(size_t i = 0; i < back.chores.size(); ++i) {
        const Chore &a = e.data().chores[i];
        const Chore &b = back.chores[i];
        CHECK(a.id == b.id && a.name == b.name && a.icon == b.icon && a.color == b.color);
        CHECK(a.schedule.type == b.schedule.type && a.schedule.weekdayMask == b.schedule.weekdayMask);
        CHECK(a.schedule.interval == b.schedule.interval && a.schedule.anchorDay == b.schedule.anchorDay);
        CHECK(a.dueMinute == b.dueMinute && a.startDay == b.startDay);
        CHECK(a.lastCompletedDay == b.lastCompletedDay && a.lastCompletedMinute == b.lastCompletedMinute);
        CHECK(a.coversDay == b.coversDay && a.completedBy == b.completedBy);
        CHECK(a.currentStreak == b.currentStreak && a.bestStreak == b.bestStreak && a.enabled == b.enabled);
    }

    // The restored engine behaves identically, including undo of the early completion.
    ChoreEngine r;
    r.data() = back;
    CHECK(statusOf(r, PLANTS, at(thu + 1)) == Status::Done);
    CHECK(r.undoLast(PLANTS));
    CHECK(statusOf(r, PLANTS, at(thu + 1)) == Status::Due);
    CHECK(statusOf(r, VAC_THU, at(thu)) == Status::Done);

    // Bad input is rejected and leaves the output alone.
    ChoreData untouched;
    untouched.nextId = 77;
    CHECK(!chorejson::parse("not json", 8, untouched));
    // v3 stands for "a version this build does not know". v2 used to serve that
    // role and is now the current schema, so the check moved up.
    CHECK(!chorejson::parse("{\"v\":3,\"chores\":[]}", 19, untouched));
    CHECK(!chorejson::parse("{\"v\":1}", 7, untouched));
    CHECK(!chorejson::parse("{\"v\":1,\"chores\":[{\"id\":1,\"sched\":{\"t\":\"bogus\"}}]}", 49, untouched));
    CHECK(untouched.nextId == 77);

    std::printf("sample JSON size: %zu bytes for %zu chores\n", json.size(), e.data().chores.size());
}

static void test_quiet_hours()
{
    // 22:00-07:00, the default: the span wraps midnight.
    CHECK(isQuietMinute(22 * 60, 22 * 60, 7 * 60));          // start is inclusive
    CHECK(isQuietMinute(23 * 60 + 59, 22 * 60, 7 * 60));
    CHECK(isQuietMinute(0, 22 * 60, 7 * 60));                 // midnight
    CHECK(isQuietMinute(6 * 60 + 59, 22 * 60, 7 * 60));
    CHECK(!isQuietMinute(7 * 60, 22 * 60, 7 * 60));           // end is exclusive
    CHECK(!isQuietMinute(12 * 60, 22 * 60, 7 * 60));
    CHECK(!isQuietMinute(21 * 60 + 59, 22 * 60, 7 * 60));

    // A span that does not wrap.
    CHECK(isQuietMinute(2 * 60, 1 * 60, 6 * 60));
    CHECK(!isQuietMinute(0, 1 * 60, 6 * 60));
    CHECK(!isQuietMinute(6 * 60, 1 * 60, 6 * 60));
    CHECK(!isQuietMinute(23 * 60, 1 * 60, 6 * 60));

    // Zero-length span means quiet hours are off, not always-on.
    CHECK(!isQuietMinute(0, 0, 0));
    CHECK(!isQuietMinute(12 * 60, 9 * 60, 9 * 60));
}

static void test_nagging()
{
    const int32_t thu = D(2026, 10, 22);
    ChoreEngine e;
    e.data() = seedChoreData(thu);

    NagConfig cfg;
    cfg.quietStartMinute = 22 * 60;
    cfg.quietEndMinute = 7 * 60;
    cfg.nagIntervalMinutes = 30;

    NagEngine nag;
    nag.setConfig(cfg);

    // Thursday noon: Vacuum (Thu) and Clean bathroom are due.
    DayTime now = at(thu, 12 * 60);
    NagDecision d = nag.evaluate(e.buildView(now), now);
    CHECK(d.attentionCount == 2);
    CHECK(!d.quiet);
    CHECK(d.shouldNag);                       // nothing nagged yet
    CHECK(d.leadChoreId != 0);

    // Having nagged, it stays quiet until the interval elapses.
    nag.markNagged(now);
    d = nag.evaluate(e.buildView(at(thu, 12 * 60 + 29)), at(thu, 12 * 60 + 29));
    CHECK(!d.shouldNag);
    d = nag.evaluate(e.buildView(at(thu, 12 * 60 + 30)), at(thu, 12 * 60 + 30));
    CHECK(d.shouldNag);                       // exactly on the interval

    // Inside quiet hours it never nags, however long it has been.
    now = at(thu, 23 * 60);
    d = nag.evaluate(e.buildView(now), now);
    CHECK(d.quiet);
    CHECK(d.attentionCount == 2);             // still outstanding, just not announced
    CHECK(!d.shouldNag);

    // Completing everything due leaves nothing to nag about.
    e.complete(VAC_THU, at(thu, 13 * 60), "Tom");
    e.complete(BATH, at(thu, 13 * 60), "Tom");
    now = at(thu, 14 * 60);
    d = nag.evaluate(e.buildView(now), now);
    CHECK(d.attentionCount == 0);
    CHECK(!d.shouldNag);
    CHECK(d.leadChoreId == 0);

    // A fresh engine nags immediately once something is overdue, even at 00:01
    // of the next interval, because reset() clears the timer.
    nag.reset();
    const int32_t fri = D(2026, 10, 23);
    now = at(fri, 12 * 60);
    d = nag.evaluate(e.buildView(now), now);
    CHECK(d.attentionCount > 0);
    CHECK(d.shouldNag);
}

static void test_minute_math()
{
    const int32_t d = D(2026, 10, 26);

    DayTime t = dateutil::addMinutes(at(d, 23 * 60 + 50), 15);
    CHECK(t.day == d + 1);
    CHECK(t.minute == 5);                       // carried past midnight

    t = dateutil::addMinutes(at(d, 10), -20);
    CHECK(t.day == d - 1);
    CHECK(t.minute == 1430);                    // borrowed back across midnight

    t = dateutil::addMinutes(at(d, 12 * 60), 60);
    CHECK(t.day == d);
    CHECK(t.minute == 13 * 60);

    CHECK(dateutil::isBefore(at(d, 10), at(d, 11)));
    CHECK(dateutil::isBefore(at(d, 1439), at(d + 1, 0)));
    CHECK(!dateutil::isBefore(at(d, 10), at(d, 10)));   // equal is not before
    CHECK(!dateutil::isBefore(at(d + 1, 0), at(d, 1439)));
}

static void test_snooze()
{
    const int32_t thu = D(2026, 10, 22);
    ChoreEngine e;
    e.data() = seedChoreData(thu);

    NagConfig cfg;
    cfg.quietStartMinute = 22 * 60;
    cfg.quietEndMinute = 7 * 60;
    cfg.nagIntervalMinutes = 30;
    NagEngine nag;
    nag.setConfig(cfg);

    DayTime now = at(thu, 12 * 60);
    CHECK(nag.evaluate(e.buildView(now), now).attentionCount == 2);

    // Snoozing holds the nag off without changing the status.
    CHECK(e.snooze(VAC_THU, dateutil::addMinutes(now, 15)));
    CHECK(statusOf(e, VAC_THU, now) == Status::Due);          // still due
    NagDecision d = nag.evaluate(e.buildView(now), now);
    CHECK(d.attentionCount == 1);                              // but not nagged
    CHECK(d.leadChoreId == BATH);

    // It expires on its own; nothing has to sweep it.
    now = at(thu, 12 * 60 + 14);
    CHECK(nag.evaluate(e.buildView(now), now).attentionCount == 1);
    now = at(thu, 12 * 60 + 15);
    CHECK(nag.evaluate(e.buildView(now), now).attentionCount == 2);

    // "Until tomorrow" covers the rest of today.
    DayTime tomorrow = at(thu + 1, 0);
    CHECK(e.snooze(VAC_THU, tomorrow));
    CHECK(nag.evaluate(e.buildView(at(thu, 23 * 60)), at(thu, 23 * 60)).attentionCount == 1);
    CHECK(nag.evaluate(e.buildView(at(thu + 1, 8 * 60)), at(thu + 1, 8 * 60)).attentionCount >= 1);

    // Cancelling brings it straight back.
    CHECK(e.snooze(BATH, dateutil::addMinutes(at(thu, 12 * 60), 60)));
    now = at(thu, 12 * 60 + 5);
    CHECK(e.evaluate(*e.find(BATH), now).snoozed);
    CHECK(e.clearSnooze(BATH));
    CHECK(!e.evaluate(*e.find(BATH), now).snoozed);

    // Completing a snoozed chore clears the snooze; undo puts it back.
    CHECK(e.snooze(BATH, dateutil::addMinutes(now, 60)));
    CHECK(e.complete(BATH, now, "Tom"));
    CHECK(e.find(BATH)->snoozeUntilDay == NO_DAY);
    CHECK(e.undoLast(BATH));
    CHECK(e.find(BATH)->snoozeUntilDay != NO_DAY);
    CHECK(e.evaluate(*e.find(BATH), now).snoozed);
}

static void test_snooze_json()
{
    const int32_t thu = D(2026, 10, 22);
    ChoreEngine e;
    e.data() = seedChoreData(thu);
    const DayTime now = at(thu, 12 * 60);
    CHECK(e.snooze(VAC_THU, dateutil::addMinutes(now, 45)));

    const std::string json = chorejson::serialize(e.data());
    ChoreData back;
    CHECK(chorejson::parse(json.c_str(), json.size(), back));

    const Chore *c = nullptr;
    for(const Chore &x : back.chores) if(x.id == VAC_THU) c = &x;
    CHECK(c != nullptr);
    CHECK(c != nullptr && c->snoozeUntilDay == dateutil::addMinutes(now, 45).day);
    CHECK(c != nullptr && c->snoozeUntilMinute == dateutil::addMinutes(now, 45).minute);

    // A v1 file still loads; it simply has no snooze.
    ChoreData old;
    const char *v1 = "{\"v\":1,\"nextId\":2,\"chores\":[{\"id\":1,\"name\":\"Mop\","
                     "\"sched\":{\"t\":\"daily\"},\"due\":-1,\"start\":0,\"cs\":3,\"bs\":4,\"en\":true}]}";
    CHECK(chorejson::parse(v1, strlen(v1), old));
    CHECK(old.chores.size() == 1);
    CHECK(old.chores.size() == 1 && old.chores[0].snoozeUntilDay == NO_DAY);
    CHECK(old.chores.size() == 1 && old.chores[0].currentStreak == 3);
}

int main()
{
    test_dates();
    test_schedules();
    test_first_boot_statuses();
    test_complete_undo_streak();
    test_early_completion();
    test_due_time_and_daily();
    test_history_prune();
    test_json();
    test_quiet_hours();
    test_nagging();
    test_minute_math();
    test_snooze();
    test_snooze_json();

    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
