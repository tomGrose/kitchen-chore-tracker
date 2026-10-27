#pragma once
/*---------------------------------------------------------------
 * Chore engine: status computation, completion, undo, streaks.
 * Self-contained (no UI, storage, web, or Arduino dependencies) so the
 * same code can later sit behind MQTT/Home Assistant (spec section 10)
 * and be unit-tested on a PC.
 *
 * Rules (all at whole-day resolution unless a due time is set):
 *  - An occurrence is a day the schedule says the chore is due. Days before
 *    the chore's startDay are ignored.
 *  - The "pending" occurrence is the earliest one after the last completion
 *    (or after the early-covered day) that is not in the future.
 *  - pending today          -> DUE (OVERDUE once its due time has passed)
 *  - pending before today   -> OVERDUE, "carried over", counted in days
 *  - no pending, done today -> DONE
 *  - otherwise              -> UPCOMING, with the next occurrence day
 *  - One completion clears every missed occurrence up to today.
 *  - Completing within `earlyWindowDays` before an occurrence also covers it.
 *  - Streak: +1 when the pending occurrence is completed on its own day (or
 *    early inside the window), reset to 1 when completed late.
 *--------------------------------------------------------------*/
#include <stdint.h>

#include <string>
#include <vector>

#include "app/chore_types.h"

enum class Status : uint8_t { Overdue = 0, Due = 1, Upcoming = 2, Done = 3 };

struct ChoreState {
    Status  status = Status::Upcoming;
    bool    snoozed = false;                // nagging held off, status unchanged
    int32_t snoozeUntilDay = dateutil::NO_DAY;
    int16_t snoozeUntilMinute = 0;
    int32_t pendingDay = dateutil::NO_DAY;  // earliest unsatisfied occurrence (Due/Overdue)
    int32_t daysOverdue = 0;                // today - pendingDay for carried-over chores
    int32_t nextDay = dateutil::NO_DAY;     // next occurrence (Upcoming/Done)
};

struct ChoreView {
    uint16_t id = 0;
    Status   status = Status::Upcoming;
    bool     snoozed = false;
    int32_t  snoozeUntilDay = dateutil::NO_DAY;
    int16_t  snoozeUntilMinute = 0;
    int32_t  daysOverdue = 0;
    int32_t  nextDay = dateutil::NO_DAY;
    uint16_t streak = 0;                    // 0 while a missed occurrence is carried over
};

class ChoreEngine {
public:
    ChoreData       &data()       { return data_; }
    const ChoreData &data() const { return data_; }

    void setEarlyWindowDays(int days) { earlyWindowDays_ = days; }

    const Chore *find(uint16_t id) const;

    ChoreState evaluate(const Chore &chore, const dateutil::DayTime &now) const;

    // Enabled chores sorted OVERDUE -> DUE -> UPCOMING -> DONE.
    std::vector<ChoreView> buildView(const dateutil::DayTime &now) const;

    // Chores still needing action today (Due + Overdue).
    int remaining(const dateutil::DayTime &now) const;

    // Mark a chore done. Returns false if it does not exist, is disabled, or is
    // already DONE.
    bool complete(uint16_t id, const dateutil::DayTime &now, const std::string &by);

    // Hold nagging off until `until`. Completing the chore clears it, as does
    // the moment passing.
    bool snooze(uint16_t id, const dateutil::DayTime &until);
    bool clearSnooze(uint16_t id);

    bool canUndo(uint16_t id) const;
    bool undoLast(uint16_t id);

    // Drop history older than keepDays.
    void pruneHistory(int32_t today, int keepDays);

private:
    Chore *findMutable(uint16_t id);

    ChoreData data_;
    int earlyWindowDays_ = 1;
};
