# Kitchen Chore Tracker

A standalone, wall-powered kitchen display that tracks recurring household chores,
shows their status at a glance, and nags until they're done. Built for an ADHD brain:
reminders live where the work happens, completion is a single tap, overdue items are
impossible to miss, and the thing stays quiet at night.

No Home Assistant required. MQTT is a planned addition, not a dependency.

## Hardware

- **Elecrow CrowPanel 7" HMI**, SKU DIS08070H, **revision V3.0**
- ESP32-S3-WROOM-1-N4R8 — 4 MB flash, 8 MB PSRAM
- 800×480 RGB parallel TFT, GT911 capacitive touch
- microSD slot (SPI), I2S speaker with onboard amp
- No onboard RTC — time comes from NTP
- Powered permanently from a 5 V / 2 A wall adapter

Pin mappings come from Elecrow's wiki for **this revision**. Other revisions differ.

## Software

PlatformIO · Arduino-ESP32 3.3.8 (pioarduino 55.03.38) · LVGL 9.1.0 · LovyanGFX 1.2.25 (patched) · ArduinoJson 7.4.2

```
src/
  main.cpp        setup/loop only
  display/        panel, touch, backlight
  ui/             LVGL layout and event forwarding — no decisions
  app/            chore engine and controller — all decisions
  storage/        persistence (SD card, LittleFS fallback)
  time/           test clock now, NTP next
tools/host_test/  engine tests that build and run on a PC
patches/          LovyanGFX RGB bus patch, applied at build time
```

`src/app` has no Arduino or LVGL dependency, so the scheduling and streak logic
builds and runs on a host:

```sh
ARDUINOJSON_DIR=/path/to/arduinojson tools/host_test/run.sh
```

## Build

```sh
pio run -t upload
pio device monitor
```

`scripts/patch_lovyangfx.py` runs pre-build and copies `patches/LovyanGFX/esp32s3/*`
over the installed library when they differ. Edit the files under `patches/`, never
the library directly.

## Two things that will bite you

**A FAT32 microSD card must be in the slot.** Chore data is written there on purpose.
Writing the ESP32-S3's internal flash disables the CPU cache for tens of milliseconds,
which starves the RGB panel's bounce-buffer refill ISR and leaves stale pixels down the
left edge of the screen. With no card the firmware falls back to LittleFS and blanks the
backlight during writes to hide it.

**`bounce_buffer_size_px` must stay non-zero** in `patches/LovyanGFX/esp32s3/Bus_RGB.cpp`.
Set it to zero and GDMA reads PSRAM directly, which it cannot sustain — constant tearing
at 24 MHz, and still tearing at 15 MHz in both FULL and DIRECT render modes.

## Documentation

| File | What it is |
|---|---|
| `CLAUDE.md` | Working agreement, and the three constraints that bite |
| `docs/progress-log.md` | Current status, every decision, and what was tried and rejected |
| `docs/firmware-reference.md` | Architecture, module map, engine semantics, invariants |
| `docs/chore-tracker-spec.md` | Product and technical spec |
| `docs/hardware-reference.md` | Pin mappings, display timings, board revision |
| `docs/chores.md` | The chore list and household settings |

## Status

Phase 3 of 6 complete and verified on hardware: chore engine, SD-backed persistence, the
main screen, Wi-Fi, NTP with DST, nagging, quiet hours, snooze and skip. Chimes deferred.
Phase 4 is the web config page.
