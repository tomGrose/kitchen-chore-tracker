#pragma once
/*---------------------------------------------------------------
 * config.h - every user-tunable value lives here.
 * Hardware facts come from hardware-reference.md (Elecrow
 * CrowPanel 7.0" DIS08070H, V3.0).
 *--------------------------------------------------------------*/
#include <stdint.h>

// 0 = off, 1 = errors, 2 = +warnings, 3 = +info, 4 = +debug
#define APP_LOG_LEVEL 3

namespace cfg {

// ---- Serial ----
constexpr uint32_t SERIAL_BAUD = 115200;

// ---- Display (800x480 RGB panel) ----
constexpr int SCREEN_W = 800;
constexpr int SCREEN_H = 480;
// Elecrow's wiki value. The panel is fed from PSRAM continuously, and the clock
// sets how much of that bandwidth it consumes: ~38 MB/s at 24 MHz against
// ~24 MB/s at 15 MHz (928x525 of timing, 768 KB a frame). What is left over is
// what LVGL has to render into, and at 24 MHz there was so little headroom that
// any sustained redraw - a tile animation, or tapping quickly - starved the
// panel's bounce-buffer refill ISR and left stale pixels down the left edge.
// 15 MHz gives back ~37% of that headroom; the cost is 31 Hz refresh instead of
// 49 Hz, which is invisible on a chore board. Do NOT raise this without
// re-testing rapid taps and the nag blink together.
constexpr uint32_t LCD_PCLK_HZ = 15000000;

// ---- Backlight ----
// Backlight is on IO2 (hardware-reference.md). Plain on/off for now;
// PWM dimming is a Phase 5 item that needs testing on this revision.
constexpr int PIN_BACKLIGHT = 2;

// ---- I2C bus shared by GT911 touch, PCA9557, and the I2C headers ----
constexpr int PIN_I2C_SDA = 19;
constexpr int PIN_I2C_SCL = 20;
constexpr uint32_t I2C_FREQ_HZ = 400000;

// ---- microSD slot (SPI) ----
// Pins from hardware-reference.md (TF card, V3.0). Chore data lives here so a
// save never writes the internal flash: a flash write disables the CPU cache
// and starves the RGB panel's refill ISR, which shows as stale pixels down the
// left edge of the screen. Needs a FAT32-formatted card.
constexpr int PIN_SD_MOSI = 11;
constexpr int PIN_SD_MISO = 13;
constexpr int PIN_SD_CLK = 12;
constexpr int PIN_SD_CS = 10;
constexpr uint32_t SD_FREQ_HZ = 20000000;      // drop to 4000000 if a card misbehaves

// ---- Touch ----
constexpr uint32_t TOUCH_POLL_MS = 30;

// ---- Main loop ----
// Short cooperative yield after each LVGL pass so the idle task and
// watchdog get time. All real work runs from LVGL timers / non-blocking checks.
constexpr uint32_t LOOP_YIELD_MS = 5;
constexpr uint32_t HEARTBEAT_LOG_MS = 60000;   // free-memory line in the serial log

// ---- Saving ----
// Saves wait until the screen has been untouched this long, so a burst of taps
// writes once. On the internal-flash fallback the write also runs with the
// backlight off, because it glitches the panel; on SD that is unnecessary.
constexpr uint32_t SAVE_DELAY_MS = 2000;
constexpr int FLASH_SETTLE_FRAMES = 2;         // frames to let scan out before the backlight returns

// ---- Chores ----
constexpr const char *DEFAULT_USER = "Tom";    // single user: the "who did it" picker is skipped
constexpr int EARLY_WINDOW_DAYS = 1;           // finishing a chore up to this many days early also covers it
constexpr int HISTORY_KEEP_DAYS = 90;

// ---- Phase 2 test clock (replaced by NTP in Phase 3) ----
constexpr bool DEV_TOOLS = true;               // on-screen day-shift / reset buttons; false for the final build
constexpr int DEV_START_YEAR = 2026;
constexpr int DEV_START_MONTH = 10;
constexpr int DEV_START_DAY = 22;              // Thursday
constexpr uint32_t DEV_START_SECOND_OF_DAY = 12 * 3600;   // 12:00 noon

// ---- Household settings from chores.md ----
// These are DEFAULTS only. The live values are in settings.json on the SD card
// (storage/settings_store); the web page edits them from Phase 4.
constexpr const char *TIMEZONE_POSIX = "MST7MDT,M3.2.0,M11.1.0";   // America/Denver
constexpr const char *NTP_SERVER = "pool.ntp.org";

// ---- Wi-Fi ----
constexpr const char *WIFI_HOSTNAME = "chores";
// Association + DHCP routinely takes 8-12 s. Anything shorter tears down a
// connection that was about to succeed and the device never links up.
constexpr uint32_t WIFI_RETRY_MIN_MS = 15000;    // first reconnect attempt after a drop
constexpr uint32_t WIFI_RETRY_MAX_MS = 60000;    // backoff ceiling
// Any epoch below this means SNTP has not answered yet (the RTC starts at 1970).
constexpr uint32_t TIME_VALID_EPOCH = 1735689600UL;   // 2025-01-01
constexpr int QUIET_START_MINUTE = 22 * 60;    // 10:00 PM
constexpr int QUIET_END_MINUTE = 7 * 60;       // 7:00 AM
constexpr int NAG_INTERVAL_MINUTES = 30;
constexpr int IDLE_DIM_SECONDS = 60;
// During quiet hours the backlight goes off once the screen has been idle this
// long, and a touch wakes it. The panel has no PWM dimming yet (Phase 5), so
// "minimal brightness" from the spec is off rather than dim. Set false in
// settings.json to leave the clock visible all night.
constexpr bool QUIET_SCREEN_OFF = true;

}  // namespace cfg
