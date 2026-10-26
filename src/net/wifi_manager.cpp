#include "net/wifi_manager.h"

#include <Arduino.h>
#include <WiFi.h>

#include "app_log.h"
#include "config.h"

namespace {

constexpr const char *TAG = "wifi";

std::string g_ssid;
std::string g_password;
bool g_configured = false;

bool g_connected = false;
bool g_state_changed = false;
uint32_t g_last_attempt_ms = 0;
uint32_t g_retry_delay_ms = cfg::WIFI_RETRY_MIN_MS;
bool g_attempted_once = false;
bool g_scan_running = false;

void start_attempt();   // defined below; the scan path falls through to it

// The reason code is the whole diagnosis: it separates "cannot see that
// network" from "wrong password" from "AP pushed us off".
const char *reason_text(uint8_t reason)
{
    switch(reason) {
    case WIFI_REASON_AUTH_EXPIRE:             return "auth expired";
    case WIFI_REASON_AUTH_LEAVE:              return "auth leave";
    case WIFI_REASON_ASSOC_EXPIRE:            return "association expired";
    case WIFI_REASON_ASSOC_TOOMANY:           return "AP has too many clients";
    case WIFI_REASON_NOT_AUTHED:              return "not authenticated";
    case WIFI_REASON_NOT_ASSOCED:             return "not associated";
    case WIFI_REASON_ASSOC_LEAVE:             return "we disconnected";
    case WIFI_REASON_STA_LEAVING:             return "we tore the attempt down to retry - expected";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:  return "4-way handshake timeout - usually a WRONG PASSWORD";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:       return "handshake timeout - usually a WRONG PASSWORD";
    case WIFI_REASON_NO_AP_FOUND:             return "no AP with that SSID - wrong name, out of range, or 5 GHz only";
    case WIFI_REASON_AUTH_FAIL:               return "auth failed - usually a WRONG PASSWORD";
    case WIFI_REASON_ASSOC_FAIL:              return "association failed";
    case WIFI_REASON_CONNECTION_FAIL:         return "connection failed";
    case WIFI_REASON_BEACON_TIMEOUT:          return "beacon timeout - weak signal";
    default:                                  return "see esp_wifi_types.h";
    }
}

void on_wifi_event(WiFiEvent_t event, WiFiEventInfo_t info)
{
    switch(event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        LOGI(TAG, "associated with the AP, waiting for an IP address");
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        LOGI(TAG, "got ip %s, rssi %d dBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
        const uint8_t reason = info.wifi_sta_disconnected.reason;
        LOGW(TAG, "disconnected: reason %u, %s", static_cast<unsigned>(reason), reason_text(reason));
        break;
    }
    default:
        break;
    }
}

// Runs once at boot, BEFORE the first connect attempt: the Wi-Fi driver rejects
// a scan while a connection is in flight. The ESP32-S3 has no 5 GHz radio
// and cannot even scan that band, so every network listed here is one it could
// join. If the configured SSID is missing from this list it is 5 GHz, hidden,
// out of range, or spelled differently.
void start_scan()
{
    LOGI(TAG, "scanning for visible 2.4 GHz networks");
    const int16_t started = WiFi.scanNetworks(true, true);   // async, include hidden
    if(started == WIFI_SCAN_FAILED) {
        // The driver refuses a scan while a connection attempt is in flight.
        LOGW(TAG, "could not start a scan right now; connecting without it");
        start_attempt();
        return;
    }
    g_scan_running = true;
}

void log_scan_results(int count)
{
    bool found = false;
    LOGW(TAG, "--- %d network(s) visible to this board (2.4 GHz only) ---", count);
    for(int i = 0; i < count; ++i) {
        const String ssid = WiFi.SSID(i);
        const bool match = ssid == g_ssid.c_str();
        if(match) found = true;
        LOGW(TAG, "  %-32s ch %2d  %4d dBm%s",
             ssid.length() ? ssid.c_str() : "(hidden)",
             WiFi.channel(i), WiFi.RSSI(i), match ? "   <= THIS IS THE CONFIGURED ONE" : "");
    }
    if(found) {
        LOGW(TAG, "\"%s\" IS visible, so the name is right - check the password and the signal level above",
             g_ssid.c_str());
    }
    else {
        LOGE(TAG, "\"%s\" is NOT among them: it is 5 GHz only, hidden, out of range, or spelled differently",
             g_ssid.c_str());
    }
    WiFi.scanDelete();
}

void start_attempt()
{
    g_last_attempt_ms = millis();
    g_attempted_once = true;
    LOGI(TAG, "connecting to \"%s\"", g_ssid.c_str());
    WiFi.begin(g_ssid.c_str(), g_password.c_str());
}

}  // namespace

namespace wifimgr {

void begin(const std::string &ssid, const std::string &password)
{
    g_ssid = ssid;
    g_password = password;
    g_configured = !g_ssid.empty();

    if(!g_configured) {
        LOGW(TAG, "no SSID in settings.json; Wi-Fi and NTP are disabled");
        return;
    }

    // Keep credentials out of internal flash: writing it glitches the panel,
    // and settings.json on the SD card is the single source of truth anyway.
    // Log the lengths, never the password: a stray space or a smart quote from
    // editing settings.json by hand is otherwise invisible.
    LOGI(TAG, "credentials from settings.json: ssid \"%s\" (%u chars), password %u chars",
         g_ssid.c_str(), static_cast<unsigned>(g_ssid.size()), static_cast<unsigned>(g_password.size()));

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.onEvent(on_wifi_event);
    WiFi.setHostname(cfg::WIFI_HOSTNAME);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);   // wall powered; keep latency low and the link stable

    // Scan before connecting: it cannot run once an attempt is in flight, and
    // knowing what is actually on air makes a failure self-explanatory.
    start_scan();
}

void tick()
{
    if(!g_configured) return;

    const bool connected = WiFi.status() == WL_CONNECTED;
    if(connected != g_connected) {
        g_connected = connected;
        g_state_changed = true;
        if(connected) {
            g_retry_delay_ms = cfg::WIFI_RETRY_MIN_MS;
            LOGI(TAG, "connected: ip %s, rssi %d dBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
        }
        else {
            LOGW(TAG, "connection lost");
        }
    }
    if(connected) return;

    // Let the diagnostic scan finish before trying again; they interfere.
    if(g_scan_running) {
        const int count = WiFi.scanComplete();
        if(count == WIFI_SCAN_RUNNING) return;
        g_scan_running = false;
        if(count < 0) {
            LOGW(TAG, "scan failed (%d)", count);
            WiFi.scanDelete();
        }
        else {
            log_scan_results(count);
        }
        start_attempt();
        return;
    }

    const uint32_t now = millis();
    if(now - g_last_attempt_ms < g_retry_delay_ms) return;

    // Still down after the backoff window: tear the attempt down and retry,
    // then back off further so a dead router does not mean constant retries.
    LOGW(TAG, "still not connected after %lu ms (status %d); retrying",
         static_cast<unsigned long>(g_retry_delay_ms), static_cast<int>(WiFi.status()));
    WiFi.disconnect();
    start_attempt();
    g_retry_delay_ms = g_retry_delay_ms * 2;
    if(g_retry_delay_ms > cfg::WIFI_RETRY_MAX_MS) g_retry_delay_ms = cfg::WIFI_RETRY_MAX_MS;
}

bool isConfigured()
{
    return g_configured;
}

bool isConnected()
{
    return g_connected;
}

bool consumeStateChange()
{
    const bool changed = g_state_changed;
    g_state_changed = false;
    return changed;
}

int rssi()
{
    return g_connected ? WiFi.RSSI() : 0;
}

std::string ip()
{
    return g_connected ? std::string(WiFi.localIP().toString().c_str()) : std::string();
}

const char *statusText()
{
    if(!g_configured) return "no Wi-Fi set";
    if(g_connected) return "online";
    if(g_scan_running) return "scanning";
    return g_attempted_once ? "connecting" : "offline";
}

}  // namespace wifimgr
