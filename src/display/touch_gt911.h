#pragma once
#include <stdint.h>

// Release the LCD/touch resets through the PCA9557 expander (required on
// V3.0) and check that the PCA9557 and GT911 answer on I2C.
// Wire1 must already be started on SDA 19 / SCL 20.
// Returns true only if both devices acknowledged.
bool touch_init();

// Poll the GT911 (rate-limited to cfg::TOUCH_POLL_MS). Returns true while a
// finger is down and fills x/y with display coordinates.
bool touch_poll(int16_t* x, int16_t* y);
