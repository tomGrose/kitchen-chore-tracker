#include "display/backlight.h"

#include <Arduino.h>

#include "app_log.h"
#include "config.h"

namespace {
constexpr const char* TAG = "backlight";
bool s_on = false;
}  // namespace

// Backlight is a plain GPIO on IO2 (hardware-reference.md).
void backlight_init(bool on)
{
    pinMode(cfg::PIN_BACKLIGHT, OUTPUT);
    backlight_set(on);
}

void backlight_set(bool on)
{
    s_on = on;
    digitalWrite(cfg::PIN_BACKLIGHT, on ? HIGH : LOW);
    LOGI(TAG, "backlight %s", on ? "ON" : "OFF");
}

bool backlight_is_on()
{
    return s_on;
}

void backlight_pause()
{
    digitalWrite(cfg::PIN_BACKLIGHT, LOW);
}

void backlight_resume()
{
    digitalWrite(cfg::PIN_BACKLIGHT, s_on ? HIGH : LOW);
}
