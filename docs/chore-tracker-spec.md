# Kitchen Chore Tracker — Product & Technical Spec

## 1. Purpose
A wall-powered, standalone kitchen display that tracks recurring household chores, shows their status at a glance, and nags until they're done. Built for an ADHD brain: the device puts reminders where the work happens, makes completion a single tap, and makes overdue items impossible to miss — while staying calm at night.

Standalone first (no Home Assistant). The architecture must allow adding Home Assistant via MQTT later without restructuring (see §9).

## 2. Hardware

### Main board
- ELECROW CrowPanel Basic 7.0" HMI (Amazon ASIN B0G1RYBZNL)
  - ESP32-S3-WROOM-1-N4R8: 4MB flash, 8MB PSRAM, Wi-Fi + BLE
  - 7" 800x480 RGB parallel TFT, capacitive touch, 5V input
- **Elecrow has shipped multiple hardware revisions with different pin mappings** for display, touch, and backlight. Pin config and display driver setup must be based on Elecrow's official wiki/example code for the exact revision printed on the board (recorded in `progress-log.md`). Do not guess pins.
- Before designing features that depend on them, confirm from the docs for this revision whether the board has: an onboard RTC, a speaker/I2S amp or buzzer output, an SD card slot, and spare GPIO/I2C headers.

### Power
- Permanently powered over USB from a wall adapter. **No battery.**
- Specify the minimum supply rating (voltage/current) for stable operation at full brightness.

### Optional add-ons (each enabled via a compile flag)
- PIR motion sensor (e.g., AM312) for wake-on-approach
- Small speaker or piezo buzzer for chimes
- DS3231 RTC module if the board has no RTC (keeps time through power outages before Wi-Fi reconnects)

## 3. Software stack
- PlatformIO + Arduino framework for ESP32-S3
- LVGL v9 (or v8 if Elecrow's examples for this revision require it — state which and why)
- UI editable in SquareLine Studio or EEZ Studio; generated UI code kept separate from app logic so regeneration never overwrites logic
- LittleFS for chore data (JSON); NVS/Preferences for settings
- ESPAsyncWebServer (or equivalent) for the config web page
- NTP for time; RTC fallback when Wi-Fi is down
- LVGL draw buffers in PSRAM
- Partition scheme sized for app + LittleFS within 4MB flash (document the partition table)

### File structure (target)
```
/src
  main.cpp
  /display   panel + touch drivers, backlight
  /ui        generated UI (SquareLine/EEZ) — no logic
  /app       chore engine, scheduler, status computation, nagging
  /storage   LittleFS JSON persistence, settings
  /web       web server, REST API, captive portal
  /power     backlight/idle management, watchdog, recovery
  /audio     chimes
  /time      NTP, RTC, timezone
  /mqtt      interface stub only (future HA integration)
/data        web assets (gzipped) for LittleFS
config.h     all user-tunable defaults
```

## 4. Chore data model
Each chore:
- `id`, `name`, optional `icon`/emoji, `color` tag
- `schedule`: daily | specific weekdays | every N days | weekly on day X | monthly on date X
- `dueTime` (optional; default "anytime today")
- `lastCompleted` timestamp, `completedBy` (optional user)
- `currentStreak`, `bestStreak`
- `enabled`

Status is **computed, never stored**:
- DONE — completed this cycle
- DUE — due today, not done
- OVERDUE — past due time, or carried over from a prior cycle
- UPCOMING — not due yet

Persistence:
- JSON on LittleFS, written atomically (temp file + rename) so power loss can't corrupt data
- Rolling completion history (~90 days) for streaks and the history view

## 5. Main screen (800x480, landscape)
- Header: large clock, date, "N left today" summary, small Wi-Fi status icon
- Grid of large chore tiles (minimum ~180x120 px touch targets), sorted OVERDUE → DUE → UPCOMING → DONE
- Tile colors: red = overdue, amber = due today, gray = upcoming, green = done
- Each tile: name, icon, status text ("Due 6pm", "2 days overdue"), streak count
- Tap → complete, with a satisfying animation and a short chime
- Long-press → details / undo
- Pagination or scrolling when chores exceed one screen
- Optional "who did it" picker (configurable user list; skipped automatically when there's only one user)

## 6. Idle / ambient mode
- After a configurable idle timeout: dim the backlight and switch to a minimal view (big clock + overdue/due counts)
- If anything is overdue, ambient mode shows a gentle red pulse or border
- Optional burn-in protection: subtly shift ambient elements every few minutes

## 7. Nagging
- When a chore becomes due or overdue: chime, wake the screen, highlight the tile
- Repeat every X minutes (configurable) until completed or snoozed
- Snooze: 15 min / 1 hr / until tomorrow
- Quiet hours (default 10pm–7am): no sounds, minimal brightness, no nags

## 8. Web config page (hosted on the ESP32)
- Reachable at `http://chores.local` (mDNS)
- Mobile-friendly single page:
  - Add / edit / delete / reorder chores and schedules
  - Manage users
  - Settings: Wi-Fi, timezone (default America/Denver), quiet hours, nag interval, brightness levels, idle timeout
  - Completion history view
  - Export / import chores as JSON for backup
- First boot or Wi-Fi failure: AP + captive portal for Wi-Fi setup
- Gzipped assets served from LittleFS; keep the page small
- Changes made on the web page update the screen live

## 9. Power, display, and connectivity management (wall-powered)
Optimize for screen longevity, reliability, and not being a distraction — not battery life.
- Backlight: full brightness when someone is near or a chore needs attention; dim after the idle timeout; very low or off during quiet hours. Wake on PIR (if installed) or touch.
- Wi-Fi stays connected full-time; NTP re-syncs periodically (e.g., every 6 hours); auto-reconnect with backoff.
- No deep sleep. Keep the main loop responsive and CPU load low.
- Power-loss recovery: on boot, reload chore data, restore time from RTC (if present) until NTP succeeds, recompute statuses so nothing is lost or falsely marked done/overdue.
- Brownout detector and watchdog enabled so the device self-recovers.

## 10. Future Home Assistant migration
- Keep chore logic in a clean, self-contained module with no UI or web dependencies.
- Define an interface/stub for an MQTT layer (HA MQTT discovery: each chore as an entity with status + a "complete" button). Do not implement MQTT yet.

## 11. Code quality
- Modular structure per §3
- No blocking delays in the main loop; correct LVGL tick/handler timing
- All user-tunable values in `config.h` or runtime settings
- Leveled serial logging, compiled out in release builds
- Comment every hardware-specific assumption with its documentation source

## 12. Phased plan
Each phase must compile and be testable on the device before moving on.

1. **Bring-up:** display + touch working, LVGL "hello world," backlight control, partition table set
2. **Chore engine:** data model, LittleFS persistence, main screen with chores from `chores.md` hard-coded as test data
3. **Time & reminders:** NTP + RTC, schedules, status computation, nagging, chimes, quiet hours, snooze
4. **Web config:** web page, REST API, captive-portal Wi-Fi setup, import/export, live screen updates
5. **Reliability:** backlight/idle management, PIR wake, Wi-Fi reconnect logic, watchdog, power-loss recovery testing
6. **Polish & enclosure:** animations, streaks, ambient mode, burn-in protection; enclosure notes (bezel cutout dimensions, mounting points, ventilation, USB cable routing and strain relief)

At the end of each phase, provide: what to test, expected behavior, common failure modes, and a `progress-log.md` update block.
