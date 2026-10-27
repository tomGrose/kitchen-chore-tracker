# Firmware Reference

Architecture and invariants as of **end of Phase 3 (2026-10-27)**. This is meant to stay
true across phases — update it when the architecture changes, not when a line changes.

## Layer map

```
main.cpp            setup/loop only; no logic
  display/          panel, touch, backlight - hardware
  ui/               LVGL layout + event forwarding - no decisions
  app/              chore engine, nagging, controller - all decisions
  net/              Wi-Fi
  storage/          backend selection, atomic writes, chore + settings stores
  time/             SNTP-backed clock
```

Two rules hold the structure together:

1. **`ui/` never decides anything.** It renders view models (`UiHeader`, `UiTile`, `UiChip`,
   `UiDetails`) and calls back into `app_controller`.
2. **`app/` has no Arduino or LVGL dependency**, except `app_controller`, which is the glue.
   That is what makes the logic host-testable. Keep it that way.

## Module responsibilities

| Module | Owns | Key entry points |
|---|---|---|
| `app/date_util` | Day-number maths, minute arithmetic across midnight | `daysFromCivil`, `civilFromDays`, `weekday`, `addMinutes`, `isBefore` |
| `app/chore_types` | `Schedule`, `Chore`, `HistoryEntry`, `ChoreData` | — |
| `app/chore_schedule` | Does a schedule fall on a day? Wording for schedules | `isOccurrence`, `nextOccurrenceOnOrAfter`, `describeSchedule`, `skipLabel` |
| `app/chore_engine` | Status, ordering, completion, undo, streaks, snooze, skip, history | `evaluate`, `buildView`, `complete`, `undoLast`, `snooze`, `skip` |
| `app/nag_engine` | Quiet hours, attention set, nag interval | `evaluate`, `markNagged`, `reset`, `isQuietMinute` |
| `app/chore_json` | Serialize/parse, schema v3 (reads v1/v2) | `serialize`, `parse` |
| `app/seed_chores` | First-boot list from `docs/chores.md` | `seedChoreData(startDay)` |
| `app/app_controller` | Board membership, view models, callbacks, deferred saves, backlight policy | `app::init`, `app::tick` |
| `net/wifi_manager` | Non-blocking connect, backoff, diagnostics | `begin`, `tick`, `isConnected`, `statusText` |
| `storage/fs_backend` | Picks SD or LittleFS once at boot | `begin`, `fs`, `name`, `writesInternalFlash` |
| `storage/atomic_file` | tmp-then-rename writes, tmp recovery | `readAll`, `write`, `recoverTmp` |
| `storage/chore_store` | Chore persistence | `load`, `save`, `wipe` |
| `storage/settings_store` | Wi-Fi, timezone, quiet hours, nag interval, dev offset | `defaults`, `load`, `save` |
| `time/app_clock` | SNTP time, local conversion, test offset | `init`, `isValid`, `now`, `devOffsetMinutes` |
| `display/display` | Panel, LVGL buffers, touch wiring | `display_init`, `display_run_flash_safe` |
| `ui/ui_main_screen` | Header, board, chip strip, details dialog, dev strip | `ui_main_create`, `ui_main_update`, `ui_main_show_details` |

## Engine semantics

The host tests lock these in. Change them deliberately.

- **Status is computed, never stored**: Overdue / Due / Upcoming / Done.
- **One completion clears all missed occurrences.** Three missed trash days become done with
  one tap — deliberately forgiving, per the ADHD framing in the spec.
- **Early completion** within `EARLY_WINDOW_DAYS` covers the *next* occurrence.
- **Streak** +1 on time or early in the window; resets to 1 when late; **unchanged by a skip**.
- **Snooze** suppresses nagging only. The chore keeps its real status and colour.
- **Skip** satisfies an occurrence without doing it. Never counts as done, never touches
  `lastCompletedDay`. Completing a skipped chore clears the skip and credits the occurrence.
- **Snoozes and skips expire lazily** — `evaluate()` ignores one whose moment has passed.
- **History** stores the previous state (including snooze and skip) so undo is exact.

## Screen model

- **Today's board** holds chores that are overdue, due, or already satisfied today. Membership
  is decided **once at day rollover** (`ensure_board`) and then left alone, so nothing enters or
  leaves while someone is using the screen. Completing early from a chip appends, never inserts.
- **Tiles are drawn in the stored order**, never re-sorted. The UI does not sort.
- **Tiles size to fill, capped at 250x190**, left-aligned.
- **Upcoming chores are chips** under the board. Tapping one opens details, not completion.
- **Done is recessed** (dark green), **Skipped is neutral grey** — never green, which would
  imply credit that was not earned.

## Invariants — don't break these

1. **`bounce_buffer_size_px` must stay non-zero.** Zero means constant tearing at any clock.
2. **Anything that writes internal flash must run inside `display_run_flash_safe()`.**
3. **No blocking `delay()` in the main loop.** `loop()` is `lv_timer_handler()` -> `app::tick()`.
4. **All LVGL calls happen on the Arduino loop task.** Deferred rebuilds use `lv_async_call`.
5. **Don't call `lv_refr_now()` from inside `app::tick()`** — mark dirty and let the loop repaint.
6. **`lgfx_panel.h` is Elecrow's config.** Pins and timings come from `docs/hardware-reference.md`,
   never from memory. The `Wire1` restart after `lcd.begin()` is load-bearing for V3.0 touch.
7. **Keep `app/` host-compilable.**
8. **Read the bandwidth budget** in `docs/progress-log.md` before adding animation.

## Build and test

```
pio run -t upload
pio device monitor

# Host tests for the pure logic
ARDUINOJSON_DIR=/path/to/arduinojson tools/host_test/run.sh
```

`scripts/patch_lovyangfx.py` runs pre-build and copies `patches/LovyanGFX/esp32s3/*` over the
installed library when they differ. Edit the files under `patches/`, never the library.

## Dev tools (remove before the final build)

Guarded by `cfg::DEV_TOOLS`. Bottom strip: `-1 hr`, `+1 hr`, `-1 day`, `+1 day`, `Reset`, and a
label showing the simulated date, the active storage backend and whether quiet hours are in
effect. The clock offset is in minutes so quiet hours can be tested without waiting until 10pm.

## Open items

- Phase 4 web config: scan-derived Wi-Fi picker, not a free-text SSID field.
- Phase 5: idle dimming outside quiet hours, PIR wake, power-loss recovery (no RTC means a
  stored last-known time is needed), watchdog.
- Chimes: I2S speaker hardware is present but untested.
