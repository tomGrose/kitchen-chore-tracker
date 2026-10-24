/*---------------------------------------------------------------
 * Kitchen Chore Tracker
 * CrowPanel 7.0" V3.0: display + touch + LVGL + chore engine.
 *--------------------------------------------------------------*/
#include <Arduino.h>
#include <lvgl.h>

#include "app/app_controller.h"
#include "app_log.h"
#include "config.h"
#include "display/backlight.h"
#include "display/display.h"

namespace {

constexpr const char *TAG = "main";

[[noreturn]] void halt(const char *reason)
{
    for(;;) {
        LOGE(TAG, "FATAL: %s", reason);
        delay(2000);
    }
}

}  // namespace

void setup()
{
    Serial.begin(cfg::SERIAL_BAUD);
    LOGI(TAG, "Chore tracker starting");

    if(!display_init()) halt("display_init failed");

    app::init();
    lv_timer_handler();     // runs the deferred first screen build
    lv_refr_now(nullptr);   // draw the first frame before the backlight comes on
    backlight_init(true);
    LOGI(TAG, "setup done");
}

void loop()
{
    static uint32_t last_heartbeat = 0;

    lv_timer_handler();
    app::tick();

    const uint32_t now = millis();
    if(now - last_heartbeat >= cfg::HEARTBEAT_LOG_MS) {
        last_heartbeat = now;
        LOGI(TAG, "uptime %lus  free heap %u KB  free PSRAM %u KB",
             static_cast<unsigned long>(now / 1000),
             static_cast<unsigned>(ESP.getFreeHeap() / 1024),
             static_cast<unsigned>(ESP.getFreePsram() / 1024));
    }

    delay(cfg::LOOP_YIELD_MS);
}
