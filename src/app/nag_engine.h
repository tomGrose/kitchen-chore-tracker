#pragma once
/*---------------------------------------------------------------
 * Nagging and quiet hours (spec section 7).
 *
 * Decides *when* to draw attention, not how: the caller owns the screen and
 * the backlight. Pure logic, no Arduino or LVGL, so the rules are testable
 * on a host.
 *
 * Audio is deliberately not here. Chimes were dropped from Phase 3; a nag is
 * currently visual only (wake the screen, highlight the tile).
 *--------------------------------------------------------------*/
#include <stdint.h>

#include <vector>

#include "app/chore_engine.h"
#include "app/date_util.h"

struct NagConfig {
    int quietStartMinute = 22 * 60;   // minutes past local midnight
    int quietEndMinute = 7 * 60;      // may be earlier than start: the span wraps midnight
    int nagIntervalMinutes = 30;
};

struct NagDecision {
    bool     quiet = false;          // inside quiet hours right now
    int      attentionCount = 0;     // due or overdue and not snoozed
    uint16_t leadChoreId = 0;        // worst offender, 0 if none; what to highlight
    bool     shouldNag = false;      // fire a nag on this tick
};

// Minutes past midnight, true inside the quiet span. Handles a span that wraps
// midnight (22:00-07:00) and one that does not (01:00-06:00).
bool isQuietMinute(int minute, int startMinute, int endMinute);

class NagEngine {
public:
    void setConfig(const NagConfig &config) { _config = config; }
    const NagConfig &config() const { return _config; }

    // Call every tick with the engine's current view. Does not mutate nag
    // timing; call markNagged() when the caller has actually nagged.
    NagDecision evaluate(const std::vector<ChoreView> &views, const dateutil::DayTime &now) const;

    void markNagged(const dateutil::DayTime &now);

    // Forget the last nag time, so the next item that needs attention is
    // flagged immediately instead of waiting out the remaining interval.
    void reset();

private:
    NagConfig _config;
    int64_t _lastNagMinute = INT64_MIN;   // absolute minutes, INT64_MIN = never
};
