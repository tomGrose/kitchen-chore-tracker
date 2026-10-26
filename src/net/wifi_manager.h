#pragma once
/*---------------------------------------------------------------
 * Wi-Fi: connect, stay connected, never block.
 *
 * Credentials come from settings.json on the SD card, NOT from NVS --
 * begin() calls WiFi.persistent(false) so the ESP-IDF does not keep its own
 * copy in internal flash. See the storage strategy section of progress-log.md.
 *
 * Reconnects with exponential backoff between cfg::WIFI_RETRY_MIN_MS and
 * cfg::WIFI_RETRY_MAX_MS. tick() must be called from the main loop and
 * returns quickly in every state.
 *--------------------------------------------------------------*/
#include <stdint.h>

#include <string>

namespace wifimgr {

// Safe to call with an empty ssid: the module then reports "not configured"
// and never tries to connect.
void begin(const std::string &ssid, const std::string &password);

void tick();

bool isConfigured();
bool isConnected();

// True on the edge where the connection state changed since the last call,
// so the caller can refresh the UI without polling it every frame.
bool consumeStateChange();

int rssi();
std::string ip();

// Short text for the screen: "online", "connecting", "offline", "no Wi-Fi set".
const char *statusText();

}  // namespace wifimgr
