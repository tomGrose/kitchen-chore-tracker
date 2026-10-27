# Progress Log

Update this at the end of every phase. This file is the canonical record of where the
project stands, what was decided, and what was tried and rejected.

## Hardware facts
- Board revision (printed on back): **V3.0**, SKU DIS08070H (ESP32-S3-WROOM-1-N4R8)
- LVGL **9.1.0**, LovyanGFX 1.2.25 (patched by scripts/patch_lovyangfx.py), Arduino-ESP32 core 3.3.8, pioarduino platform 55.03.38
- Onboard RTC: no - Speaker output: yes (I2S, onboard amp, untested) - SD slot: yes (SPI)
- microSD pins: MOSI = IO11, MISO = IO13, CLK = IO12, CS = IO10. Own SPI bus (FSPI), no contention with the RGB panel, touch or I2S
- Power: get a 5V / 2A+ wall adapter before permanent install. Ruled out as a cause of the display artifacts (2026-10-23) — a wall charger made no difference

## Current status
- **Phase 3 complete**, verified on device 2026-10-27. Phase 4 (web config) not started
- **A FAT32 microSD card must be in the slot**, with Wi-Fi credentials in `settings.json` on the card. Without a card the firmware falls back to internal flash and the post-save artifacts return
- `chores.json` is **schema v3**. v1 and v2 still load
- Chimes were dropped from Phase 3 by choice. A nag is visual only

## Phase checklist
- [x] Phase 1 — Bring-up (2026-10-22)
- [x] Phase 2 — Chore engine (2026-10-24)
- [x] Phase 3 — Time & reminders (2026-10-27): NTP, Wi-Fi, nagging, quiet hours, snooze, skip. Chimes deferred
- [ ] Phase 4 — Web config
- [ ] Phase 5 — Reliability
- [ ] Phase 6 — Polish & enclosure

## Decisions made
<!-- Format: YYYY-MM-DD — decision — reason -->
- 2026-10-22 — Base project on Elecrow V3.0 PlatformIO70 example, LVGL 9.1 — newer than the wiki's 8.3 examples
- 2026-10-22 — ~~Pixel clock stays at 24 MHz~~ **Superseded 2026-10-26: now 15 MHz. See the display bandwidth section**
- 2026-10-22 — Partition table (partitions.csv): nvs 20 KB, otadata, app 2 MB, LittleFS 1.875 MB (label "spiffs"), coredump 64 KB. No second OTA slot, USB flashing only
- 2026-10-22 — Serial at 115200; leveled logging via include/app_log.h, level set by APP_LOG_LEVEL in config.h
- 2026-10-22 — Touching a dark screen wakes the backlight and swallows that touch, so wake never presses a button
- 2026-10-22 — Backlight is plain GPIO on/off for now; PWM dimming is a Phase 5 item
- 2026-10-23 — **Chore data is stored on the microSD card, not internal flash** — writing internal flash disables the CPU cache for tens of ms, which starves the RGB panel's bounce-buffer refill ISR and leaves stale pixels down the left edge. SD traffic is ordinary SPI and leaves the cache alone. LittleFS remains as an automatic fallback, and that path blanks the backlight during a write
- 2026-10-23 — **The RGB panel requires the bounce buffer** (`bounce_buffer_size_px = width * 10`). With it at 0, GDMA reads PSRAM directly and cannot keep up: constant tearing at both 24 MHz and 15 MHz, in both render modes. Do not remove it
- 2026-10-23 — ~~LVGL render mode stays FULL — DIRECT was tried and did not fix the tearing~~ **Wrong, corrected 2026-10-26: that DIRECT test ran with the bounce buffer disabled, so the panel was starved regardless and the test could not have told us anything about DIRECT. Render mode is now DIRECT**
- 2026-10-23 — Saves are deferred until the screen has been untouched for `cfg::SAVE_DELAY_MS` (2 s) — a burst of taps writes once. Kept for SD too, to limit card wear
- 2026-10-24 — UI is hand-written LVGL, not SquareLine/EEZ output — the screen is simple enough that generated code would add a regeneration step for no gain. The spec's "generated UI stays separate from logic" rule still holds: `ui_main_screen` has layout and event forwarding only, all decisions live in `app_controller`
- 2026-10-24 — Serial port is not committed in platformio.ini — pin it per machine with `PLATFORMIO_UPLOAD_PORT` or `--upload-port COMx`
- 2026-10-26 — **`esp_lcd_rgb_panel_restart()` does NOT clear the artifacts** — tested directly. There is no cheap recovery for flash writes we do not control, so avoiding internal flash stays the strategy
- 2026-10-26 — **Wi-Fi credentials live in settings.json on the SD card**, with `WiFi.persistent(false)` so the ESP-IDF keeps no copy in NVS. Scan before the first connect attempt and log the ESP-IDF disconnect reason, so a failure names itself
- 2026-10-26 — **Nothing computes chore status until SNTP has answered.** No RTC means the clock reads 1970 at boot and everything would look years overdue; the screen shows a waiting state and an empty grid instead, and first-run seeding waits too so schedule anchors are not set in 1970
- 2026-10-26 — **A nag blinks every overdue and due tile, discretely, three on/off cycles.** All of them, because if three things are due and only one moves the other two do not register. Discrete rather than animated, because of the bandwidth budget. Deliberately unlike the completion pop
- 2026-10-26 — **Quiet hours switch the backlight off once idle**, since there is no PWM dimming yet. `quietScreenOff: false` keeps the clock lit overnight
- 2026-10-26 — **Chimes dropped from Phase 3.** Revisit with the I2S speaker in a later phase
- 2026-10-27 — **A snooze suppresses nagging but does not change status.** A snoozed chore stays Overdue and stays red; it just stops blinking. Hiding genuine lateness because someone pressed snooze would make the display lie, which is the one thing this device cannot afford to do
- 2026-10-27 — **Snoozes and skips expire lazily** — `evaluate()` ignores a snooze whose moment has passed. Nothing needs a timer or a sweep, and an expiry cannot be missed while the device is off
- 2026-10-27 — **chores.json versioning**: bump the schema and keep older versions readable whenever the model grows. v2 added snooze, v3 added skip
- 2026-10-27 — **Today's board is decided once at day rollover and then left alone.** Completing a chore never reorders the grid, so a tile cannot move under your finger mid-tap and positions stay learnable. The only time tiles move is the new day
- 2026-10-27 — **Completed tiles stay in place, recessed** (dark green, dim text) rather than moving to the end. The eye skips them when scanning for what is left
- 2026-10-27 — **Chores not due today are a compact chip strip under the board**, not full tiles. Tapping a chip opens the details rather than completing, so doing something early is deliberate
- 2026-10-27 — **Tiles have a maximum size** (250x190) and only shrink below it. Filling the board with two enormous tiles read as a mistake. Left-aligned, not centred, so the first chore keeps its corner and an early completion (which appends a tile) does not shift the others
- 2026-10-27 — **Skip satisfies an occurrence without doing it.** The streak is left alone: skipping is an honest choice and should not be punished like forgetting, but it earns no credit. The tile reads "Skipped" in neutral grey, never green. Completing a skipped chore still works and credits the skipped occurrence

## Display bandwidth — the binding constraint

**This cost more time than anything else on this project. Read it before adding anything that animates or redraws often.**

The RGB panel is fed from PSRAM continuously, and whatever bandwidth it does not consume is what LVGL has to render into. That budget is tight, and three different symptoms all trace back to it:

| Symptom | Cause |
|---|---|
| Stale pixels in a band down the **left edge**, persisting until that region is redrawn | The bounce-buffer refill ISR missed its deadline — either a flash write disabled the cache, or LVGL was saturating PSRAM |
| **Constant tearing**, worst during redraws | No bounce buffer: GDMA cannot feed the panel from PSRAM directly at any clock |
| An unpainted strip along one tile's edge, only during an animation | Not bandwidth — a DIRECT-mode partial-repaint mismatch when the animation changed the object's geometry |

Current working configuration, all three parts load-bearing:
- **Bounce buffer `width * 10`** — required
- **LVGL `RENDER_MODE_DIRECT`** — FULL repaints all 384,000 pixels on any invalidation; DIRECT repaints only what changed
- **Pixel clock 15 MHz** — ~24 MB/s for the panel instead of ~38 MB/s at 24 MHz, giving back ~37% of the headroom. Refresh is 31 Hz, which is invisible here

**Rules for future UI work:**
- Prefer **discrete state changes over smooth animations**. A smooth animation repaints its target ~30 times a second; eight tiles of that saturates the bus. Six opaque repaints do not.
- **Never animate anything semi-transparent** over a large area. Alpha forces LVGL to redraw everything underneath *and* read the frame buffer back to blend — roughly double the traffic of an opaque fill. This was tried and it was worse than what it replaced.
- **Avoid animating properties that change an object's geometry** (border width, size, position) in DIRECT mode. The dirty area LVGL computes may not match what actually changed, leaving unpainted strips at clip boundaries. Colour and content changes are safe.
- `transform_scale` renders the object to a temporary layer and blits it scaled. Fine for one tile occasionally (the completion pop); not for many at once.
- If something tears or shows stale pixels, **work out the per-frame cost before changing the code**: area x repaints per second, against roughly 24 MB/s of spare bandwidth. Every wrong guess on this project came from skipping that arithmetic.

**Still unexplained:** why flash-write artifacts persisted indefinitely rather than clearing on the next frame, given `CONFIG_LCD_RGB_RESTART_IN_VSYNC` is 1 in every Arduino sdkconfig variant. The model is incomplete; treat the SD-card strategy as a sound workaround rather than a full understanding.

## Storage strategy

**The problem this answers:** Wi-Fi writes internal flash whether we want it to or not. `WiFi.begin()` persists credentials to NVS unless `WiFi.persistent(false)` is called, and ESP-IDF stores PHY calibration in the `phy` NVS namespace and refreshes it occasionally. Credentials we can stop. Calibration writes we cannot.

1. **Done:** settings on SD behind a shared storage layer (`storage/fs_backend` picks the backend, `storage/atomic_file` does tmp-then-rename, both stores sit on top). **No `Preferences`/NVS calls anywhere in feature code** — changing backend later is one file.
2. **Done and negative:** `esp_lcd_rgb_panel_restart()` does not clear the artifacts.
3. **Deferred:** the XIP rebuild. `CONFIG_SPIRAM_XIP_FROM_PSRAM` plus `CONFIG_SPI_FLASH_AUTO_SUSPEND` keeps the PSRAM cache alive during flash operations, but both are baked into the precompiled Arduino libraries, so it means building Arduino as an ESP-IDF component.

**OTA forces the issue eventually.** A single 2 MB app slot and no second OTA slot means wireless updates need repartitioning *and* writing megabytes while the screen is live. If OTA matters, XIP becomes required. If USB flashing is acceptable, it may never be needed.

## Working practices that have paid off

- **Keep `src/app` free of Arduino and LVGL.** The engine, schedules, JSON, nagging and skip logic all build and run on a PC: `tools/host_test/run.sh`, 2428 checks. Every logic bug so far was caught there rather than on the board
- **Host-test the boundaries, not the happy path.** Quiet hours wrapping midnight, minute arithmetic borrowing across days, snooze expiry on the exact minute, old schema versions loading into a new build
- **Lay out rows of buttons by computing across the available width**, never by hand-picked x positions. Doing it by hand put two buttons on top of each other
- **Git on Windows needs its lock files removable.** If a git command reports `Operation not permitted` on `.git/*.lock`, clear them before the next git command or it fails with "File exists"

## Known issues / TODO
- Upload fails with "Access is denied" if something holds the COM port. Stop it before flashing
- Fonts enabled: Montserrat 14 (default), 20, 24, 40, 48. Add sizes in platformio.ini build_flags as needed
- partitions.csv still reserves 1.875 MB for LittleFS, now only a fallback. Reclaim it in Phase 4 if the web server needs the space
- `cfg::DEV_TOOLS` and the dev strip must be turned off for the final build
- No SD card hot-swap: the card is mounted once at boot
- Every tap destroys and recreates all board tiles including their text. Updating them in place would cut that to the ones that changed — the structural fix if the bandwidth budget gets tight again
- Hand-editing `settings.json` is error-prone: a typographic apostrophe in an SSID cost a debugging session. Phase 4's web page should offer a **scan-derived network picker**, not a free-text SSID field
- After a power cut with no network the device is unusable until Wi-Fi returns, because there is no RTC and no stored last-known time. Phase 5 power-loss recovery
- The spec's section 6 ambient mode and "wake the screen" assume idle dimming, which does not exist outside quiet hours yet. Phase 5
