#pragma once
/*---------------------------------------------------------------
 * Main chore screen (spec section 5): header, tile grid, details dialog,
 * and the Phase 2 dev strip. Hand-written for now; it holds layout and
 * event forwarding only. All decisions are made by app/app_controller.
 * Everything here must be called from the LVGL task (the Arduino loop).
 *--------------------------------------------------------------*/
#include <stdint.h>

#include <vector>

enum class UiTileState : uint8_t { Overdue, Due, Upcoming, Done };

struct UiTile {
    uint16_t    id = 0;
    char        name[40] = {};
    char        status[40] = {};
    UiTileState state = UiTileState::Upcoming;
    uint16_t    streak = 0;
};

struct UiHeader {
    char time[12] = {};
    char date[32] = {};
    char summary[24] = {};
    char wifi[24] = {};     // short connection status shown under the summary
    bool time_valid = false;   // false: show a waiting state, not chore status
    char dev[64] = {};      // dev strip text, ignored when dev tools are off
};

struct UiDetails {
    uint16_t id = 0;
    char     name[40] = {};
    char     schedule[48] = {};
    char     last[64] = {};
    char     streak[48] = {};
    char     snooze[56] = {};   // current snooze state, empty when not snoozed
    bool     can_undo = false;
    bool     can_snooze = false;   // false for chores that are not due or overdue
};

struct UiCallbacks {
    void (*on_tile_tap)(uint16_t id) = nullptr;
    void (*on_tile_long_press)(uint16_t id) = nullptr;
    void (*on_undo)(uint16_t id) = nullptr;
    // kind: 0 = 15 minutes, 1 = 1 hour, 2 = until tomorrow, 3 = cancel a snooze
    void (*on_snooze)(uint16_t id, int kind) = nullptr;
    void (*on_dev_shift_minutes)(int delta) = nullptr;
    void (*on_dev_reset)() = nullptr;
};

void ui_main_create(const UiCallbacks &callbacks);

// Safe to call from inside an LVGL event handler: the redraw is deferred.
// `pulse_id` makes that tile do a short "completed" pop animation (0 = none).
// `nag` makes every overdue/due tile flash its border instead - a deliberately
// different motion from the completion pop, so one gesture never means both
// "well done" and "you are late".
void ui_main_update(const UiHeader &header, const std::vector<UiTile> &tiles, uint16_t pulse_id,
                    bool nag = false);

void ui_main_show_details(const UiDetails &details);
