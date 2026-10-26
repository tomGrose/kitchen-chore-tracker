#pragma once
/*---------------------------------------------------------------
 * Local wall-clock time for the chore engine.
 *
 * Time comes from SNTP over Wi-Fi, converted to local time by the POSIX TZ
 * string in settings (so DST is handled by the C library, not by us). The
 * board has no RTC, so after a power cut the time is unknown until NTP
 * answers: isValid() is false until then and callers must not compute chore
 * status, or everything would look wildly overdue.
 *
 * devOffsetMinutes shifts the clock for testing, in minutes so quiet hours
 * can be exercised without waiting until 10pm. It is persisted in settings on
 * the SD card, never NVS.
 *--------------------------------------------------------------*/
#include <time.h>

#include <string>

#include "app/date_util.h"

namespace appclock {

// Starts SNTP. Safe to call before Wi-Fi is up; it syncs once a link exists.
void init(const std::string &timezonePosix, const std::string &ntpServer, int offsetMinutes);

// False until SNTP has answered at least once. now() is meaningless until then.
bool isValid();

// Current local day and minute, including the test offset.
dateutil::DayTime now();

// Seconds since the epoch, 0 while invalid.
time_t epoch();

int  devOffsetMinutes();
void setDevOffsetMinutes(int minutes);

}  // namespace appclock
