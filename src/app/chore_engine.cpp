#include "app/chore_engine.h"

#include <algorithm>

#include "app/chore_schedule.h"

using dateutil::DayTime;
using dateutil::NO_DAY;

const Chore *ChoreEngine::find(uint16_t id) const
{
    for(const Chore &c : data_.chores) {
        if(c.id == id) return &c;
    }
    return nullptr;
}

Chore *ChoreEngine::findMutable(uint16_t id)
{
    for(Chore &c : data_.chores) {
        if(c.id == id) return &c;
    }
    return nullptr;
}

ChoreState ChoreEngine::evaluate(const Chore &chore, const DayTime &now) const
{
    ChoreState state;

    // A snooze that has passed is simply ignored; it is cleared lazily rather
    // than needing a sweep, so nothing has to run on a timer to expire it.
    if(chore.snoozeUntilDay != NO_DAY) {
        DayTime until;
        until.day = chore.snoozeUntilDay;
        until.minute = chore.snoozeUntilMinute;
        if(dateutil::isBefore(now, until)) {
            state.snoozed = true;
            state.snoozeUntilDay = chore.snoozeUntilDay;
            state.snoozeUntilMinute = chore.snoozeUntilMinute;
        }
    }

    // Everything up to and including this day is already satisfied, whether by
    // doing it, by an early completion covering it, or by a deliberate skip.
    int32_t satisfiedThrough = NO_DAY;
    if(chore.lastCompletedDay != NO_DAY) satisfiedThrough = chore.lastCompletedDay;
    if(chore.coversDay != NO_DAY && chore.coversDay > satisfiedThrough) satisfiedThrough = chore.coversDay;
    if(chore.skippedDay != NO_DAY && chore.skippedDay > satisfiedThrough) satisfiedThrough = chore.skippedDay;

    // True while the skip is the most recent thing satisfying this chore, so
    // the screen can say "Skipped" instead of claiming it was done.
    state.skipped = chore.skippedDay != NO_DAY && chore.skippedDay == satisfiedThrough;

    int32_t from = chore.startDay;
    if(satisfiedThrough != NO_DAY && satisfiedThrough + 1 > from) from = satisfiedThrough + 1;

    const int32_t pending = nextOccurrenceOnOrAfter(chore.schedule, from);

    if(pending != NO_DAY && pending <= now.day) {
        state.pendingDay = pending;
        if(pending < now.day) {
            state.status = Status::Overdue;
            state.daysOverdue = now.day - pending;
        }
        else if(chore.dueMinute >= 0 && now.minute >= chore.dueMinute) {
            state.status = Status::Overdue;   // due today, but its time has passed
            state.daysOverdue = 0;
        }
        else {
            state.status = Status::Due;
        }
        return state;
    }

    state.nextDay = pending;   // NO_DAY if the schedule has no future occurrence
    const bool doneToday = chore.lastCompletedDay == now.day || chore.coversDay == now.day ||
                           chore.skippedDay == now.day;
    state.status = doneToday ? Status::Done : Status::Upcoming;
    return state;
}

namespace {

int rankOf(Status s)
{
    return static_cast<int>(s);
}

}  // namespace

std::vector<ChoreView> ChoreEngine::buildView(const DayTime &now) const
{
    std::vector<ChoreView> view;
    view.reserve(data_.chores.size());

    for(const Chore &c : data_.chores) {
        if(!c.enabled) continue;
        const ChoreState st = evaluate(c, now);
        ChoreView v;
        v.id = c.id;
        v.status = st.status;
        v.skipped = st.skipped;
        v.snoozed = st.snoozed;
        v.snoozeUntilDay = st.snoozeUntilDay;
        v.snoozeUntilMinute = st.snoozeUntilMinute;
        v.daysOverdue = st.daysOverdue;
        v.nextDay = st.nextDay;
        v.streak = (st.status == Status::Overdue && st.daysOverdue > 0) ? 0 : c.currentStreak;
        view.push_back(v);
    }

    // stable_sort keeps the original chore order for ties.
    std::stable_sort(view.begin(), view.end(), [](const ChoreView &a, const ChoreView &b) {
        if(rankOf(a.status) != rankOf(b.status)) return rankOf(a.status) < rankOf(b.status);
        if(a.status == Status::Overdue) return a.daysOverdue > b.daysOverdue;   // worst first
        if(a.status == Status::Upcoming) {
            const int32_t an = (a.nextDay == NO_DAY) ? INT32_MAX : a.nextDay;
            const int32_t bn = (b.nextDay == NO_DAY) ? INT32_MAX : b.nextDay;
            return an < bn;                                                      // soonest first
        }
        return false;
    });
    return view;
}

int ChoreEngine::remaining(const DayTime &now) const
{
    int count = 0;
    for(const Chore &c : data_.chores) {
        if(!c.enabled) continue;
        const Status s = evaluate(c, now).status;
        if(s == Status::Due || s == Status::Overdue) ++count;
    }
    return count;
}

bool ChoreEngine::complete(uint16_t id, const DayTime &now, const std::string &by)
{
    Chore *c = findMutable(id);
    if(c == nullptr || !c->enabled) return false;

    const ChoreState before = evaluate(*c, now);
    // A skipped chore can still be done: changing your mind and doing it anyway
    // should record properly, not be refused because the skip satisfied it.
    if(before.status == Status::Done && !before.skipped) return false;

    HistoryEntry h;
    h.choreId = id;
    h.day = now.day;
    h.minute = now.minute;
    h.by = by;
    h.prevCompletedDay = c->lastCompletedDay;
    h.prevCompletedMinute = c->lastCompletedMinute;
    h.prevCoversDay = c->coversDay;
    h.prevStreak = c->currentStreak;
    h.prevBest = c->bestStreak;
    h.prevSnoozeUntilDay = c->snoozeUntilDay;
    h.prevSnoozeUntilMinute = c->snoozeUntilMinute;
    h.prevSkippedDay = c->skippedDay;

    // Clear the skip before working out the streak, so the occurrence that was
    // skipped is the one being credited rather than the one after it.
    if(before.skipped) c->skippedDay = NO_DAY;
    const ChoreState st = evaluate(*c, now);

    uint16_t streak = c->currentStreak;
    int32_t covers = NO_DAY;

    if(st.status == Status::Due || st.status == Status::Overdue) {
        // On time = completed on the occurrence's own day. Late resets to 1.
        streak = (st.pendingDay == now.day) ? static_cast<uint16_t>(streak + 1) : 1;
    }
    else if(st.nextDay != NO_DAY && st.nextDay - now.day <= earlyWindowDays_) {
        // Early, inside the window: also covers the coming occurrence.
        streak = static_cast<uint16_t>(streak + 1);
        covers = st.nextDay;
    }
    // Earlier than the window: recorded as done today, streak and schedule untouched.

    c->lastCompletedDay = now.day;
    c->lastCompletedMinute = now.minute;
    c->coversDay = covers;
    c->completedBy = by;
    c->currentStreak = streak;
    if(streak > c->bestStreak) c->bestStreak = streak;
    c->snoozeUntilDay = NO_DAY;   // done is done; nothing left to snooze
    c->snoozeUntilMinute = 0;
    c->skippedDay = NO_DAY;       // actually doing it overrides having skipped it

    data_.history.push_back(h);
    return true;
}

bool ChoreEngine::skip(uint16_t id, const DayTime &now)
{
    Chore *c = findMutable(id);
    if(c == nullptr || !c->enabled) return false;

    const ChoreState st = evaluate(*c, now);
    // Whichever occurrence is in question: the one that is due or late, or the
    // one coming up if this is being skipped ahead of time.
    const int32_t target = st.pendingDay != NO_DAY ? st.pendingDay : st.nextDay;
    if(target == NO_DAY) return false;
    if(st.status == Status::Done) return false;

    c->skippedDay = target;
    c->snoozeUntilDay = NO_DAY;   // nothing left to hold off
    c->snoozeUntilMinute = 0;
    return true;
}

bool ChoreEngine::unskip(uint16_t id)
{
    Chore *c = findMutable(id);
    if(c == nullptr || c->skippedDay == NO_DAY) return false;
    c->skippedDay = NO_DAY;
    return true;
}

bool ChoreEngine::snooze(uint16_t id, const DayTime &until)
{
    Chore *c = findMutable(id);
    if(c == nullptr || !c->enabled) return false;
    c->snoozeUntilDay = until.day;
    c->snoozeUntilMinute = until.minute;
    return true;
}

bool ChoreEngine::clearSnooze(uint16_t id)
{
    Chore *c = findMutable(id);
    if(c == nullptr) return false;
    c->snoozeUntilDay = NO_DAY;
    c->snoozeUntilMinute = 0;
    return true;
}

bool ChoreEngine::canUndo(uint16_t id) const
{
    for(const HistoryEntry &h : data_.history) {
        if(h.choreId == id) return true;
    }
    return false;
}

bool ChoreEngine::undoLast(uint16_t id)
{
    Chore *c = findMutable(id);
    if(c == nullptr) return false;

    for(size_t i = data_.history.size(); i-- > 0;) {
        const HistoryEntry &h = data_.history[i];
        if(h.choreId != id) continue;

        c->lastCompletedDay = h.prevCompletedDay;
        c->lastCompletedMinute = h.prevCompletedMinute;
        c->coversDay = h.prevCoversDay;
        c->currentStreak = h.prevStreak;
        c->bestStreak = h.prevBest;
        c->snoozeUntilDay = h.prevSnoozeUntilDay;
        c->snoozeUntilMinute = h.prevSnoozeUntilMinute;
        c->skippedDay = h.prevSkippedDay;
        data_.history.erase(data_.history.begin() + static_cast<std::ptrdiff_t>(i));
        return true;
    }
    return false;
}

void ChoreEngine::pruneHistory(int32_t today, int keepDays)
{
    const int32_t cutoff = today - keepDays;
    data_.history.erase(
        std::remove_if(data_.history.begin(), data_.history.end(),
                       [cutoff](const HistoryEntry &h) { return h.day < cutoff; }),
        data_.history.end());
}
