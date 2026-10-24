#pragma once
/*---------------------------------------------------------------
 * Chore data persistence (spec sections 3 and 4).
 * Files:  /chores.json  current data
 *         /chores.tmp   being written / awaiting rename
 *         /chores.bad   a file that failed to parse, kept for inspection
 *
 * Data lives on the microSD card, NOT the internal flash. Writing internal
 * flash disables the CPU cache for tens of milliseconds, which starves the RGB
 * panel's bounce-buffer refill ISR and leaves stale pixels down the left edge
 * of the screen (diagnosed 2026-10-23). SD traffic is ordinary SPI and leaves
 * the cache alone. With no card inserted this falls back to LittleFS on the
 * internal flash, and saves then blank the backlight to hide the glitch.
 *
 * Writes go to the .tmp file first and are renamed over the real file, so a
 * power cut never leaves a half-written chores.json. If power fails in the
 * short window between removing the old file and the rename, load() finds the
 * complete .tmp file and promotes it.
 *--------------------------------------------------------------*/
#include <stdint.h>

#include "app/chore_types.h"

namespace chore_store {

enum class Backend : uint8_t { None, SdCard, Flash };

// Mount the SD card, or LittleFS if no card is present.
bool begin();

Backend backend();
const char *backendName();

// True only for the LittleFS fallback: the caller must hide the screen glitch.
bool writesInternalFlash();

// false: no valid data stored; `out` is left untouched.
bool load(ChoreData &out);

bool save(const ChoreData &data);

// Delete all stored chore files.
void wipe();

}  // namespace chore_store
