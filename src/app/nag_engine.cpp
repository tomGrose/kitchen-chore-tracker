#include "app/nag_engine.h"

namespace {

// Absolute minutes since the epoch day, so comparisons work across midnight.
int64_t absoluteMinutes(const dateutil::DayTime &t)
{
    return static_cast<int64_t>(t.day) * 1440 + t.minute;
}

}  // namespace

bool isQuietMinute(int minute, int startMinute, int endMinute)
{
    if(startMinute == endMinute) return false;          // zero-length span: never quiet
    if(startMinute < endMinute) {                        // e.g. 01:00-06:00, same day
        return minute >= startMinute && minute < endMinute;
    }
    return minute >= startMinute || minute < endMinute;  // e.g. 22:00-07:00, wraps midnight
}

NagDecision NagEngine::evaluate(const std::vector<ChoreView> &views, const dateutil::DayTime &now) const
{
    NagDecision d;
    d.quiet = isQuietMinute(now.minute, _config.quietStartMinute, _config.quietEndMinute);

    // buildView() already sorts OVERDUE -> DUE -> UPCOMING -> DONE, so the
    // first match is the one most worth pointing at.
    for(const ChoreView &v : views) {
        if(v.status != Status::Overdue && v.status != Status::Due) continue;
        if(v.snoozed) continue;   // still outstanding, just asked to be left alone
        ++d.attentionCount;
        if(d.leadChoreId == 0) d.leadChoreId = v.id;
    }

    if(d.attentionCount == 0 || d.quiet) return d;

    const int64_t nowMinutes = absoluteMinutes(now);
    if(_lastNagMinute == INT64_MIN) {
        d.shouldNag = true;   // nothing nagged yet this run
    }
    else if(nowMinutes - _lastNagMinute >= _config.nagIntervalMinutes) {
        d.shouldNag = true;
    }
    return d;
}

void NagEngine::markNagged(const dateutil::DayTime &now)
{
    _lastNagMinute = absoluteMinutes(now);
}

void NagEngine::reset()
{
    _lastNagMinute = INT64_MIN;
}
