#include "app/app_controller.h"

#include <Arduino.h>
#include <lvgl.h>   // LV_SYMBOL_* strings used in tile text

#include <stdio.h>
#include <string>
#include <vector>

#include "app/chore_engine.h"
#include "app/nag_engine.h"
#include "app/chore_schedule.h"
#include "app/seed_chores.h"
#include "app_log.h"
#include "config.h"
#include "display/backlight.h"
#include "display/display.h"
#include "net/wifi_manager.h"
#include "storage/chore_store.h"
#include "storage/fs_backend.h"
#include "storage/settings_store.h"
#include "time/app_clock.h"
#include "ui/ui_main_screen.h"

using dateutil::DayTime;
using dateutil::NO_DAY;

namespace app {

namespace {

constexpr const char *TAG = "app";

ChoreEngine g_engine;
NagEngine g_nag;
Settings g_settings;
bool g_storage_ok = false;
DayTime g_last_shown = {-1, -1};
bool g_time_ready = false;   // set on the first valid clock reading after boot

// Today's board: which chores are on screen, and in what order. Decided once at
// day rollover and then left alone, so completing something never reorders the
// grid and a tile never moves under your finger. Chores completed early from
// the upcoming strip are appended, never inserted.
std::vector<uint16_t> g_board_ids;
int32_t g_board_day = NO_DAY;
uint16_t g_pulse_id = 0;
bool g_nag_flash = false;   // flash the attention tiles on the next refresh
uint32_t g_last_tick = 0;

const char *const kWeekdayLong[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char *const kMonthShort[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// "live", "+2h", "-1d3h"
void format_offset(char *out, size_t size, int minutes)
{
    if(minutes == 0) { snprintf(out, size, "live"); return; }
    const char sign = minutes < 0 ? '-' : '+';
    const int m = minutes < 0 ? -minutes : minutes;
    const int days = m / 1440;
    const int hours = (m % 1440) / 60;
    if(days && hours) snprintf(out, size, "%c%dd%dh", sign, days, hours);
    else if(days)     snprintf(out, size, "%c%dd", sign, days);
    else              snprintf(out, size, "%c%dh", sign, hours);
}

// "2:05 PM"
void format_time(char *out, size_t size, int minute)
{
    const int hour = minute / 60;
    const int hour12 = (hour % 12 == 0) ? 12 : hour % 12;
    snprintf(out, size, "%d:%02d %s", hour12, minute % 60, hour >= 12 ? "PM" : "AM");
}

// "Oct 22"
void format_month_day(char *out, size_t size, int32_t day)
{
    int year = 0;
    unsigned month = 0;
    unsigned dom = 0;
    dateutil::civilFromDays(day, year, month, dom);
    snprintf(out, size, "%s %u", kMonthShort[month - 1], dom);
}

// "Thursday, Oct 22"
void format_long_date(char *out, size_t size, int32_t day)
{
    char md[16];
    format_month_day(md, sizeof(md), day);
    snprintf(out, size, "%s, %s", kWeekdayLong[dateutil::weekday(day)], md);
}

// "3:15 PM" today, "tomorrow", or a weekday further out.
void format_snooze_until(char *out, size_t size, const ChoreView &v, const DayTime &now)
{
    if(v.snoozeUntilDay == now.day) {
        char t[16];
        format_time(t, sizeof(t), v.snoozeUntilMinute);
        snprintf(out, size, "Snoozed till %s", t);
    }
    else if(v.snoozeUntilDay == now.day + 1 && v.snoozeUntilMinute == 0) {
        snprintf(out, size, "Snoozed till tomorrow");
    }
    else {
        snprintf(out, size, "Snoozed till %s", weekdayShortName(dateutil::weekday(v.snoozeUntilDay)));
    }
}

void tile_status_text(char *out, size_t size, const ChoreView &v, const Chore &chore, const DayTime &now)
{
    // While snoozed the tile says so instead of how late it is. The colour
    // still carries the urgency, and the details dialog still has the full
    // picture - this is the only feedback that the snooze actually took.
    if(v.skipped) {
        snprintf(out, size, "Skipped");
        return;
    }
    if(v.snoozed) {
        format_snooze_until(out, size, v, now);
        return;
    }

    switch(v.status) {
    case Status::Overdue:
        if(v.daysOverdue >= 1) {
            snprintf(out, size, "%d day%s overdue", static_cast<int>(v.daysOverdue), v.daysOverdue == 1 ? "" : "s");
        }
        else {
            char t[16];
            format_time(t, sizeof(t), chore.dueMinute);
            snprintf(out, size, "Overdue since %s", t);
        }
        break;
    case Status::Due:
        if(chore.dueMinute >= 0) {
            char t[16];
            format_time(t, sizeof(t), chore.dueMinute);
            snprintf(out, size, "Due %s", t);
        }
        else {
            snprintf(out, size, "Due today");
        }
        break;
    case Status::Upcoming:
        if(v.nextDay == NO_DAY) snprintf(out, size, "Nothing planned");
        else if(v.nextDay == now.day + 1) snprintf(out, size, "Tomorrow");
        else if(v.nextDay - now.day <= 6) snprintf(out, size, "%s", weekdayShortName(dateutil::weekday(v.nextDay)));
        else format_month_day(out, size, v.nextDay);
        break;
    case Status::Done:
        snprintf(out, size, LV_SYMBOL_OK " Done");
        break;
    }
}

UiTileState to_ui_state(Status s)
{
    switch(s) {
    case Status::Overdue:  return UiTileState::Overdue;
    case Status::Due:      return UiTileState::Due;
    case Status::Upcoming: return UiTileState::Upcoming;
    case Status::Done:     return UiTileState::Done;
    }
    return UiTileState::Upcoming;
}

const char *status_name(Status s)
{
    switch(s) {
    case Status::Overdue:  return "OVERDUE";
    case Status::Due:      return "DUE";
    case Status::Upcoming: return "upcoming";
    case Status::Done:     return "done";
    }
    return "?";
}

// One serial line per chore, handy for checking the logic against the screen.
void log_statuses(const DayTime &now)
{
    char date[32];
    char time[16];
    format_long_date(date, sizeof(date), now.day);
    format_time(time, sizeof(time), now.minute);
    LOGI(TAG, "--- %s %s ---", date, time);
    for(const ChoreView &v : g_engine.buildView(now)) {
        const Chore *c = g_engine.find(v.id);
        LOGI(TAG, "%-8s %-16s streak %u%s", status_name(v.status), c ? c->name.c_str() : "?",
             static_cast<unsigned>(v.streak), v.daysOverdue > 0 ? " (carried over)" : "");
    }
}

// Saving is deferred until the screen has been untouched for cfg::SAVE_DELAY_MS,
// so a burst of taps writes once. Changes made in that window are lost only if
// power is cut before the save runs. Chore data goes to the SD card, which does
// not disturb the panel; only the internal-flash fallback needs the screen
// blanked while it writes.
bool g_save_pending = false;

void save_now(void *)
{
    const uint32_t started = millis();
    if(!chore_store::save(g_engine.data())) LOGE(TAG, "SAVE FAILED: changes will be lost on reboot");
    LOGI(TAG, "saved in %lu ms", static_cast<unsigned long>(millis() - started));
}

void flush_save_now()
{
    g_save_pending = false;
    if(fsbackend::writesInternalFlash()) display_run_flash_safe(save_now, nullptr);
    else save_now(nullptr);
}

void persist()
{
    if(!g_storage_ok) return;
    g_save_pending = true;
}

void flush_save_if_due()
{
    if(!g_save_pending) return;
    if(lv_display_get_inactive_time(nullptr) < cfg::SAVE_DELAY_MS) return;
    flush_save_now();
}

bool on_board(uint16_t id)
{
    for(uint16_t x : g_board_ids) if(x == id) return true;
    return false;
}

void add_to_board(uint16_t id)
{
    if(!on_board(id)) g_board_ids.push_back(id);
}

// Rebuilt only when the day changes. buildView is sorted worst-first, so the
// board starts in urgency order and then stays put for the rest of the day.
void ensure_board(const DayTime &now)
{
    if(g_board_day == now.day) return;
    g_board_ids.clear();
    for(const ChoreView &v : g_engine.buildView(now)) {
        if(v.status != Status::Upcoming) g_board_ids.push_back(v.id);
    }
    g_board_day = now.day;
    LOGI(TAG, "board for the day: %u chore(s)", static_cast<unsigned>(g_board_ids.size()));
}

// "Tomorrow", "Fri", or "Nov 4" - what goes on an upcoming chip.
void format_when(char *out, size_t size, int32_t day, const DayTime &now)
{
    if(day == NO_DAY) snprintf(out, size, "someday");
    else if(day == now.day + 1) snprintf(out, size, "Tomorrow");
    else if(day - now.day <= 6) snprintf(out, size, "%s", weekdayShortName(dateutil::weekday(day)));
    else format_month_day(out, size, day);
}

// Shown until SNTP answers. The board has no RTC, so before the first sync the
// date is 1970 and every chore would look years overdue - better to show
// nothing than to show a screen full of false alarms.
void refresh_waiting_ui()
{
    UiHeader header;
    snprintf(header.time, sizeof(header.time), "--:--");
    snprintf(header.date, sizeof(header.date), "Waiting for the time");
    snprintf(header.summary, sizeof(header.summary), " ");
    snprintf(header.wifi, sizeof(header.wifi), "%s", wifimgr::statusText());

    char offset[16];
    format_offset(offset, sizeof(offset), appclock::devOffsetMinutes());
    snprintf(header.dev, sizeof(header.dev), "%s - %s", offset, fsbackend::name());
    header.time_valid = false;

    ui_main_update(header, {}, {}, 0);
}

void refresh_ui(const DayTime &now)
{
    UiHeader header;
    format_time(header.time, sizeof(header.time), now.minute);
    format_long_date(header.date, sizeof(header.date), now.day);
    snprintf(header.wifi, sizeof(header.wifi), "%s", wifimgr::statusText());
    header.time_valid = true;

    const int remaining = g_engine.remaining(now);
    if(remaining == 0) snprintf(header.summary, sizeof(header.summary), "All done!");
    else snprintf(header.summary, sizeof(header.summary), "%d left today", remaining);

    char dev_day[16];
    char offset[16];
    format_month_day(dev_day, sizeof(dev_day), now.day);
    format_offset(offset, sizeof(offset), appclock::devOffsetMinutes());
    // Backend shown here because a missing SD card degrades silently to flash.
    // Quiet state too, since during quiet hours the screen is normally dark.
    snprintf(header.dev, sizeof(header.dev), "%s %s - %s%s", dev_day, offset, fsbackend::name(),
             isQuietMinute(now.minute, g_settings.quietStartMinute, g_settings.quietEndMinute) ? " - quiet" : "");

    ensure_board(now);
    const std::vector<ChoreView> views = g_engine.buildView(now);

    // The board is drawn in its stored order, not the sorted one.
    std::vector<UiTile> tiles;
    for(uint16_t id : g_board_ids) {
        const ChoreView *v = nullptr;
        for(const ChoreView &candidate : views) if(candidate.id == id) v = &candidate;
        const Chore *c = g_engine.find(id);
        if(v == nullptr || c == nullptr) continue;   // chore removed since the board was built

        UiTile t;
        t.id = id;
        snprintf(t.name, sizeof(t.name), "%s", c->name.c_str());
        tile_status_text(t.status, sizeof(t.status), *v, *c, now);
        t.state = v->skipped ? UiTileState::Skipped : to_ui_state(v->status);
        t.streak = v->streak;
        tiles.push_back(t);
    }

    // Everything else, compactly, soonest first (buildView already orders
    // upcoming chores that way).
    std::vector<UiChip> chips;
    for(const ChoreView &v : views) {
        if(chips.size() >= 6) break;
        if(on_board(v.id)) continue;
        const Chore *c = g_engine.find(v.id);
        if(c == nullptr) continue;
        char when[16];
        format_when(when, sizeof(when), v.nextDay, now);
        UiChip chip;
        chip.id = v.id;
        snprintf(chip.text, sizeof(chip.text), "%s - %s", when, c->name.c_str());
        chips.push_back(chip);
    }

    ui_main_update(header, tiles, chips, g_pulse_id, g_nag_flash);
    g_pulse_id = 0;
    g_nag_flash = false;
    g_last_shown = now;
}

void apply_nag_config()
{
    NagConfig c;
    c.quietStartMinute = g_settings.quietStartMinute;
    c.quietEndMinute = g_settings.quietEndMinute;
    c.nagIntervalMinutes = g_settings.nagIntervalMinutes;
    g_nag.setConfig(c);
}

// The panel has no PWM dimming yet, so "minimal brightness" during quiet hours
// is off-once-idle, with a touch waking it (display.cpp swallows that touch so
// waking never presses a button). Outside quiet hours the screen stays lit;
// idle dimming during the day is a Phase 5 item.
void update_backlight(const NagDecision &decision)
{
    bool want_on = true;
    if(decision.quiet && g_settings.quietScreenOff) {
        const uint32_t idle_ms = lv_display_get_inactive_time(nullptr);
        want_on = idle_ms < static_cast<uint32_t>(g_settings.idleDimSeconds) * 1000;
    }
    if(want_on != backlight_is_on()) backlight_set(want_on);
}

// ---- UI callbacks ----

void on_tile_tap(uint16_t id)
{
    if(!appclock::isValid()) return;
    const DayTime now = appclock::now();
    const Chore *c = g_engine.find(id);
    if(g_engine.complete(id, now, cfg::DEFAULT_USER)) {
        LOGI(TAG, "completed \"%s\" (streak %u)", c ? c->name.c_str() : "?", c ? static_cast<unsigned>(c->currentStreak) : 0U);
        add_to_board(id);   // doing something early should show up, at the end
        g_pulse_id = id;
        persist();
    }
    else {
        LOGI(TAG, "tap on \"%s\" ignored (already done)", c ? c->name.c_str() : "?");
    }
    refresh_ui(now);
}

void on_show_details(uint16_t id)
{
    if(!appclock::isValid()) return;
    const Chore *c = g_engine.find(id);
    if(c == nullptr) return;

    UiDetails d;
    d.id = id;
    snprintf(d.name, sizeof(d.name), "%s", c->name.c_str());
    snprintf(d.schedule, sizeof(d.schedule), "%s", describeSchedule(c->schedule).c_str());

    if(c->lastCompletedDay == NO_DAY) {
        snprintf(d.last, sizeof(d.last), "Last done: never");
    }
    else {
        char md[16];
        char t[16];
        format_month_day(md, sizeof(md), c->lastCompletedDay);
        format_time(t, sizeof(t), c->lastCompletedMinute);
        snprintf(d.last, sizeof(d.last), "Last done: %s %s, %s",
                 weekdayShortName(dateutil::weekday(c->lastCompletedDay)), md, t);
    }
    snprintf(d.streak, sizeof(d.streak), "Streak: %u   (best %u)",
             static_cast<unsigned>(c->currentStreak), static_cast<unsigned>(c->bestStreak));
    d.can_undo = g_engine.canUndo(id);

    const DayTime now = appclock::now();
    const ChoreState st = g_engine.evaluate(*c, now);
    d.can_snooze = st.status == Status::Overdue || st.status == Status::Due;
    // Offered for anything not already done, which is how an upcoming chore
    // reached from the chip strip gets completed early.
    d.can_complete = st.status != Status::Done || st.skipped;
    // Skipping is offered for anything not already satisfied, including an
    // upcoming chore reached from the chip strip - "I know I'm not doing it".
    d.can_skip = st.status != Status::Done || st.skipped;
    d.is_skipped = st.skipped;
    snprintf(d.skip_label, sizeof(d.skip_label), "%s", skipLabel(c->schedule));
    if(st.snoozed) {
        ChoreView v;
        v.snoozeUntilDay = st.snoozeUntilDay;
        v.snoozeUntilMinute = st.snoozeUntilMinute;
        format_snooze_until(d.snooze, sizeof(d.snooze), v, now);
    }

    ui_main_show_details(d);
}

void on_undo(uint16_t id)
{
    if(!appclock::isValid()) return;
    if(g_engine.undoLast(id)) {
        LOGI(TAG, "undid last completion of chore %u", static_cast<unsigned>(id));
        persist();
    }
    refresh_ui(appclock::now());
}

// Settings are small and rare; write them straight through, blanking the screen
// only on the internal-flash fallback.
void persist_settings()
{
    if(!g_storage_ok) return;
    if(fsbackend::writesInternalFlash()) {
        display_run_flash_safe([](void *) { settings_store::save(g_settings); }, nullptr);
    }
    else if(!settings_store::save(g_settings)) {
        LOGE(TAG, "could not save settings");
    }
}

void on_snooze(uint16_t id, int kind)
{
    if(!appclock::isValid()) return;
    const DayTime now = appclock::now();
    const Chore *c = g_engine.find(id);

    bool changed = false;
    switch(kind) {
    case 0: changed = g_engine.snooze(id, dateutil::addMinutes(now, 15)); break;
    case 1: changed = g_engine.snooze(id, dateutil::addMinutes(now, 60)); break;
    case 2: {
        // "Tomorrow" means the start of tomorrow, so it stops bothering you for
        // the rest of today. Quiet hours cover the small hours either way.
        DayTime tomorrow;
        tomorrow.day = now.day + 1;
        tomorrow.minute = 0;
        changed = g_engine.snooze(id, tomorrow);
        break;
    }
    default: changed = g_engine.clearSnooze(id); break;
    }

    if(changed) {
        LOGI(TAG, "%s \"%s\"", kind == 3 ? "cancelled the snooze on" : "snoozed",
             c ? c->name.c_str() : "?");
        persist();
    }
    refresh_ui(now);
}

void on_skip(uint16_t id)
{
    if(!appclock::isValid()) return;
    const DayTime now = appclock::now();
    const Chore *c = g_engine.find(id);
    if(c == nullptr) return;
    const bool was_skipped = g_engine.evaluate(*c, now).skipped;

    const bool changed = was_skipped ? g_engine.unskip(id) : g_engine.skip(id, now);
    if(changed) {
        LOGI(TAG, "%s \"%s\"", was_skipped ? "un-skipped" : "skipped", c->name.c_str());
        add_to_board(id);   // skipping an upcoming chore should be visible
        persist();
    }
    refresh_ui(now);
}

void on_dev_shift_minutes(int delta)
{
    g_settings.devOffsetMinutes += delta;
    appclock::setDevOffsetMinutes(g_settings.devOffsetMinutes);
    // A jump in time must not look like a nag interval quietly elapsing.
    g_nag.reset();
    persist_settings();
    if(!appclock::isValid()) {
        refresh_waiting_ui();
        return;
    }
    const DayTime now = appclock::now();
    log_statuses(now);
    refresh_ui(now);
}

void on_dev_reset()
{
    LOGW(TAG, "dev reset: wiping stored chores and the test-clock offset");
    chore_store::wipe();
    g_settings.devOffsetMinutes = 0;
    appclock::setDevOffsetMinutes(0);
    g_nag.reset();
    g_board_day = NO_DAY;
    persist_settings();
    if(!appclock::isValid()) {
        // Reseeding needs today's date; the clock will do it when it syncs.
        g_engine.data() = ChoreData{};
        refresh_waiting_ui();
        return;
    }
    const DayTime now = appclock::now();
    g_engine.data() = seedChoreData(now.day);
    persist();
    flush_save_now();
    log_statuses(now);
    refresh_ui(now);
}

}  // namespace

void init()
{
    g_storage_ok = fsbackend::begin();

    g_settings = settings_store::defaults();
    if(g_storage_ok && !settings_store::load(g_settings)) {
        LOGI(TAG, "no stored settings; writing defaults to %s", fsbackend::name());
        settings_store::save(g_settings);
    }

    wifimgr::begin(g_settings.wifiSsid, g_settings.wifiPassword);
    appclock::init(g_settings.timezonePosix, g_settings.ntpServer, g_settings.devOffsetMinutes);
    apply_nag_config();
    g_engine.setEarlyWindowDays(cfg::EARLY_WINDOW_DAYS);

    ChoreData loaded;
    if(g_storage_ok && chore_store::load(loaded)) {
        g_engine.data() = loaded;
    }
    else if(!appclock::isValid()) {
        // Seeding needs today's date to set the anchors, so it waits for the clock.
        LOGI(TAG, "no saved chores; seeding deferred until the time is known");
    }
    else {
        LOGI(TAG, "no saved chores; seeding the list from chores.md");
        g_engine.data() = seedChoreData(appclock::now().day);
        if(g_storage_ok) save_now(nullptr);   // backlight is still off at boot, so no need to hide the write
    }

    UiCallbacks callbacks;
    callbacks.on_tile_tap = on_tile_tap;
    callbacks.on_show_details = on_show_details;
    callbacks.on_mark_done = on_tile_tap;   // same action, reached from the details dialog
    callbacks.on_skip = on_skip;
    callbacks.on_undo = on_undo;
    callbacks.on_snooze = on_snooze;
    callbacks.on_dev_shift_minutes = on_dev_shift_minutes;
    callbacks.on_dev_reset = on_dev_reset;
    ui_main_create(callbacks);
    refresh_waiting_ui();
}

// Everything that needs a real date, run once when SNTP first answers.
void on_time_became_valid()
{
    const DayTime now = appclock::now();
    LOGI(TAG, "clock is live; computing chore status");

    if(g_engine.data().chores.empty()) {
        LOGI(TAG, "seeding the list from chores.md");
        g_engine.data() = seedChoreData(now.day);
        persist();
    }
    g_engine.pruneHistory(now.day, cfg::HISTORY_KEEP_DAYS);
    log_statuses(now);
    refresh_ui(now);
}

void tick()
{
    wifimgr::tick();
    flush_save_if_due();

    const uint32_t ms = millis();
    if(ms - g_last_tick < 1000) return;
    g_last_tick = ms;

    const bool time_valid = appclock::isValid();
    if(time_valid && !g_time_ready) {
        g_time_ready = true;
        on_time_became_valid();
        wifimgr::consumeStateChange();
        return;
    }

    const bool wifi_changed = wifimgr::consumeStateChange();
    if(!time_valid) {
        if(wifi_changed) refresh_waiting_ui();
        return;
    }

    const DayTime now = appclock::now();
    const NagDecision decision = g_nag.evaluate(g_engine.buildView(now), now);

    // Nothing outstanding: forget the timer so the next chore that falls due
    // is flagged at once instead of waiting out the rest of the interval.
    if(decision.attentionCount == 0) g_nag.reset();

    bool redraw = wifi_changed || now.day != g_last_shown.day || now.minute != g_last_shown.minute;
    if(decision.shouldNag) {
        const Chore *lead = g_engine.find(decision.leadChoreId);
        LOGI(TAG, "nag: %d chore(s) need attention, starting with \"%s\"",
             decision.attentionCount, lead ? lead->name.c_str() : "?");
        backlight_set(true);
        g_nag_flash = true;
        g_nag.markNagged(now);
        redraw = true;
    }

    update_backlight(decision);
    if(redraw) refresh_ui(now);
}

}  // namespace app
