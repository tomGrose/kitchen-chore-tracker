#include "storage/settings_store.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include "app_log.h"
#include "config.h"
#include "storage/atomic_file.h"
#include "storage/fs_backend.h"

namespace {

constexpr const char *TAG = "settings";
constexpr const char *FILE_MAIN = "/settings.json";
constexpr const char *FILE_TMP = "/settings.tmp";
constexpr int SCHEMA_VERSION = 1;

}  // namespace

namespace settings_store {

Settings defaults()
{
    Settings s;
    s.wifiSsid = "";
    s.wifiPassword = "";
    s.timezonePosix = cfg::TIMEZONE_POSIX;
    s.ntpServer = cfg::NTP_SERVER;
    s.quietStartMinute = cfg::QUIET_START_MINUTE;
    s.quietEndMinute = cfg::QUIET_END_MINUTE;
    s.nagIntervalMinutes = cfg::NAG_INTERVAL_MINUTES;
    s.idleDimSeconds = cfg::IDLE_DIM_SECONDS;
    s.chimesEnabled = true;
    s.devOffsetDays = 0;
    return s;
}

bool load(Settings &out)
{
    fs::FS *filesystem = fsbackend::fs();
    if(filesystem == nullptr) return false;

    if(!filesystem->exists(FILE_MAIN)) {
        atomicfile::recoverTmp(*filesystem, FILE_MAIN, FILE_TMP);
    }
    std::string text;
    if(!atomicfile::readAll(*filesystem, FILE_MAIN, text)) return false;

    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, text);
    if(error) {
        LOGE(TAG, "%s is unreadable (%s); using defaults", FILE_MAIN, error.c_str());
        return false;
    }
    if(doc["version"].as<int>() != SCHEMA_VERSION) {
        LOGW(TAG, "settings schema %d, expected %d; using defaults",
             doc["version"].as<int>(), SCHEMA_VERSION);
        return false;
    }

    // Each field falls back to whatever `out` already holds, so a settings file
    // written by an older build keeps working when new fields are added.
    out.wifiSsid = doc["wifi"]["ssid"] | out.wifiSsid.c_str();
    out.wifiPassword = doc["wifi"]["password"] | out.wifiPassword.c_str();
    out.timezonePosix = doc["time"]["tz"] | out.timezonePosix.c_str();
    out.ntpServer = doc["time"]["ntp"] | out.ntpServer.c_str();
    out.quietStartMinute = doc["reminders"]["quietStart"] | out.quietStartMinute;
    out.quietEndMinute = doc["reminders"]["quietEnd"] | out.quietEndMinute;
    out.nagIntervalMinutes = doc["reminders"]["nagInterval"] | out.nagIntervalMinutes;
    out.idleDimSeconds = doc["reminders"]["idleDim"] | out.idleDimSeconds;
    out.chimesEnabled = doc["reminders"]["chimes"] | out.chimesEnabled;
    out.devOffsetDays = doc["dev"]["offsetDays"] | out.devOffsetDays;

    LOGI(TAG, "loaded settings: ssid \"%s\", tz %s, quiet %02d:%02d-%02d:%02d, nag %d min",
         out.wifiSsid.c_str(), out.timezonePosix.c_str(),
         out.quietStartMinute / 60, out.quietStartMinute % 60,
         out.quietEndMinute / 60, out.quietEndMinute % 60,
         out.nagIntervalMinutes);
    return true;
}

bool save(const Settings &settings)
{
    fs::FS *filesystem = fsbackend::fs();
    if(filesystem == nullptr) return false;

    JsonDocument doc;
    doc["version"] = SCHEMA_VERSION;
    doc["wifi"]["ssid"] = settings.wifiSsid;
    doc["wifi"]["password"] = settings.wifiPassword;
    doc["time"]["tz"] = settings.timezonePosix;
    doc["time"]["ntp"] = settings.ntpServer;
    doc["reminders"]["quietStart"] = settings.quietStartMinute;
    doc["reminders"]["quietEnd"] = settings.quietEndMinute;
    doc["reminders"]["nagInterval"] = settings.nagIntervalMinutes;
    doc["reminders"]["idleDim"] = settings.idleDimSeconds;
    doc["reminders"]["chimes"] = settings.chimesEnabled;
    doc["dev"]["offsetDays"] = settings.devOffsetDays;

    std::string json;
    serializeJson(doc, json);
    if(!atomicfile::write(*filesystem, FILE_MAIN, FILE_TMP, json.data(), json.size())) return false;
    LOGD(TAG, "saved %u bytes of settings", static_cast<unsigned>(json.size()));
    return true;
}

void wipe()
{
    fs::FS *filesystem = fsbackend::fs();
    if(filesystem == nullptr) return;
    atomicfile::removeAll(*filesystem, FILE_MAIN, FILE_TMP);
}

}  // namespace settings_store
