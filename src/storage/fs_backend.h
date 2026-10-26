#pragma once
/*---------------------------------------------------------------
 * Picks the filesystem everything else persists to, once, at boot.
 *
 * microSD if a FAT32 card is present, otherwise LittleFS on the internal
 * flash. Writing internal flash disables the CPU cache for tens of ms, which
 * starves the RGB panel's bounce-buffer refill ISR and leaves stale pixels
 * down the left edge of the screen; SD traffic is ordinary SPI and leaves the
 * cache alone. See the storage strategy section of progress-log.md.
 *
 * Nothing above this layer names a backend. Keep it that way: if settings ever
 * move to NVS, or an sdkconfig rebuild makes flash writes safe, this is the
 * only file that has to change.
 *--------------------------------------------------------------*/
#include <FS.h>
#include <stdint.h>

namespace fsbackend {

enum class Backend : uint8_t { None, SdCard, Flash };

// Mount. Call once, before any store.
bool begin();

// Null until begin() has succeeded.
fs::FS *fs();

Backend backend();
const char *name();

// True only for the LittleFS fallback: writes disturb the panel, so the caller
// hides them (see display_run_flash_safe).
bool writesInternalFlash();

}  // namespace fsbackend
