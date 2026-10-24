#pragma once
/*---------------------------------------------------------------
 * Time source for the chore engine.
 *
 * PHASE 2 TEST CLOCK: there is no NTP yet (Phase 3). Time starts at the
 * date configured in config.h (cfg::DEV_START_*) on every boot and runs in
 * real time. The on-screen dev buttons shift the date by whole days; that
 * offset is saved in NVS so it survives reboots. Phase 3 replaces the body
 * of now() with NTP/RTC time; nothing else has to change.
 *--------------------------------------------------------------*/
#include "app/date_util.h"

namespace appclock {

void init();

// Current local day and minute.
dateutil::DayTime now();

int  devOffsetDays();
void devShiftDays(int delta);
void devReset();

}  // namespace appclock
