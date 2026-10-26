#pragma once
/*---------------------------------------------------------------
 * Runtime settings: Wi-Fi, time, reminders. Everything a user can change
 * without reflashing. Defaults come from config.h, which in turn comes from
 * chores.md; the web page (Phase 4) edits these.
 *
 * Stored as JSON next to the chore data, NOT in NVS. See the storage strategy
 * section of progress-log.md: NVS is internal flash and writing it glitches
 * the panel. Wi-Fi credentials live here too, which is why Phase 3 calls
 * WiFi.persistent(false) and stops the ESP-IDF saving its own copy.
 *--------------------------------------------------------------*/
#include <stdint.h>

#include <string>

struct Settings {
    // ---- Wi-Fi ----
    std::string wifiSsid;
    std::string wifiPassword;

    // ---- Time ----
    std::string timezonePosix;   // POSIX TZ string, e.g. "MST7MDT,M3.2.0,M11.1.0"
    std::string ntpServer;

    // ---- Reminders ----
    int quietStartMinute = 0;    // minutes past local midnight
    int quietEndMinute = 0;
    int nagIntervalMinutes = 0;
    int idleDimSeconds = 0;
    bool chimesEnabled = true;

    // ---- Dev ----
    // Test-clock day offset. Lives here rather than NVS so the dev buttons do
    // not write internal flash. Always 0 in a release build.
    int devOffsetDays = 0;
};

namespace settings_store {

// Settings filled from config.h, used when nothing is stored yet.
Settings defaults();

// Returns false if nothing valid was stored; `out` is then left as passed in,
// so seed it with defaults() first.
bool load(Settings &out);

bool save(const Settings &settings);

void wipe();

}  // namespace settings_store
