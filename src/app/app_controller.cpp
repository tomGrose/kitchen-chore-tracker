#include "app/app_controller.h"

#include <Arduino.h>
#include <lvgl.h>   // LV_SYMBOL_* strings used in tile text

#include <stdio.h>
#include <string>
#include <vector>

#include "app/chore_engine.h"
#include "app/chore_schedule.h"
#include "app/seed_chores.h"
#include "app_log.h"
#include "config.h"
#include "display/display.h"
#include "storage/chore_store.h"
#include "time/app_clock.h"
#include "ui/ui_main_screen.h"

using dateutil::DayTime;
using dateutil::NO_DAY;

namespace app {

namespace {

constexpr const char *TAG = "app";

ChoreEngine g_engine;
bool g_storage_ok = false;
DayTime g_last_shown = {-1, -1};
uint16_t g_pulse_id = 0;
uint32_t g_last_tick = 0;
bool g_dev_anim = true;   // dev toggles for isolating screen glitches (Phase 2 testing)
bool g_dev_save = true;

const char *const kWeekdayLong[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char *const kMonthShort[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

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

void tile_status_text(char *out, size_t size, const ChoreView &v, const Chore &chore, const DayTime &now)
{
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
    if(chore_store::writesInternalFlash()) display_run_flash_safe(save_now, nullptr);
    else save_now(nullptr);
}

void persist()
{
    if(!g_storage_ok) return;
    if(!g_dev_save) {
        LOGI(TAG, "save skipped (dev toggle is OFF)");
        return;
    }
    g_save_pending = true;
}

void flush_save_if_due()
{
    if(!g_save_pending) return;
    if(lv_display_get_inactive_time(nullptr) < cfg::SAVE_DELAY_MS) return;
    flush_save_now();
}

void refresh_ui(const DayTime &now)
{
    UiHeader header;
    format_time(header.time, sizeof(header.time), now.minute);
    format_long_date(header.date, sizeof(header.date), now.day);

    const int remaining = g_engine.remaining(now);
    if(remaining == 0) snprintf(header.summary, sizeof(header.summary), "All done!");
    else snprintf(header.summary, sizeof(header.summary), "%d left today", remaining);

    char dev_day[16];
    format_month_day(dev_day, sizeof(dev_day), now.day);
    // Backend shown here because a missing SD card degrades silently to flash.
    snprintf(header.dev, sizeof(header.dev), "%s (%+dd) - %s", dev_day, appclock::devOffsetDays(),
             chore_store::backendName());
    header.dev_anim = g_dev_anim;
    header.dev_save = g_dev_save;

    std::vector<UiTile> tiles;
    for(const ChoreView &v : g_engine.buildView(now)) {
        const Chore *c = g_engine.find(v.id);
        if(c == nullptr) continue;
        UiTile t;
        t.id = v.id;
        snprintf(t.name, sizeof(t.name), "%s", c->name.c_str());
        tile_status_text(t.status, sizeof(t.status), v, *c, now);
        t.state = to_ui_state(v.status);
        t.streak = v.streak;
        tiles.push_back(t);
    }

    ui_main_update(header, tiles, g_pulse_id);
    g_pulse_id = 0;
    g_last_shown = now;
}

// ---- UI callbacks ----

void on_tile_tap(uint16_t id)
{
    const DayTime now = appclock::now();
    const Chore *c = g_engine.find(id);
    if(g_engine.complete(id, now, cfg::DEFAULT_USER)) {
        LOGI(TAG, "completed \"%s\" (streak %u)", c ? c->name.c_str() : "?", c ? static_cast<unsigned>(c->currentStreak) : 0U);
        g_pulse_id = g_dev_anim ? id : 0;
        persist();
    }
    else {
        LOGI(TAG, "tap on \"%s\" ignored (already done)", c ? c->name.c_str() : "?");
    }
    refresh_ui(now);
}

void on_tile_long_press(uint16_t id)
{
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

    ui_main_show_details(d);
}

void on_undo(uint16_t id)
{
    if(g_engine.undoLast(id)) {
        LOGI(TAG, "undid last completion of chore %u", static_cast<unsigned>(id));
        persist();
    }
    refresh_ui(appclock::now());
}

void on_dev_shift_days(int delta)
{
    // The test-clock offset lives in NVS (flash), so write it with the screen blanked.
    display_run_flash_safe([](void *p) { appclock::devShiftDays(*static_cast<int *>(p)); }, &delta);
    const DayTime now = appclock::now();
    log_statuses(now);
    refresh_ui(now);
}

void on_dev_toggle(int which)
{
    if(which == 0) g_dev_anim = !g_dev_anim;
    else g_dev_save = !g_dev_save;
    LOGI(TAG, "dev toggles: animation %s, saving %s", g_dev_anim ? "ON" : "OFF", g_dev_save ? "ON" : "OFF");
    refresh_ui(appclock::now());
}

void on_dev_reset()
{
    LOGW(TAG, "dev reset: wiping stored chores and test-clock offset");
    display_run_flash_safe([](void *) {
        chore_store::wipe();
        appclock::devReset();
    }, nullptr);
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
    g_storage_ok = chore_store::begin();
    appclock::init();
    g_engine.setEarlyWindowDays(cfg::EARLY_WINDOW_DAYS);

    const DayTime now = appclock::now();

    ChoreData loaded;
    if(g_storage_ok && chore_store::load(loaded)) {
        g_engine.data() = loaded;
    }
    else {
        LOGI(TAG, "no saved chores; seeding the list from chores.md");
        g_engine.data() = seedChoreData(now.day);
        if(g_storage_ok) save_now(nullptr);   // backlight is still off at boot, so no need to hide the write
    }
    g_engine.pruneHistory(now.day, cfg::HISTORY_KEEP_DAYS);

    UiCallbacks callbacks;
    callbacks.on_tile_tap = on_tile_tap;
    callbacks.on_tile_long_press = on_tile_long_press;
    callbacks.on_undo = on_undo;
    callbacks.on_dev_shift_days = on_dev_shift_days;
    callbacks.on_dev_reset = on_dev_reset;
    callbacks.on_dev_toggle = on_dev_toggle;
    ui_main_create(callbacks);

    log_statuses(now);
    refresh_ui(now);
}

void tick()
{
    flush_save_if_due();

    const uint32_t ms = millis();
    if(ms - g_last_tick < 1000) return;
    g_last_tick = ms;

    const DayTime now = appclock::now();
    if(now.day != g_last_shown.day || now.minute != g_last_shown.minute) refresh_ui(now);
}

}  // namespace app
