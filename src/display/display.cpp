#include "display/display.h"

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>

#include "app_log.h"
#include "config.h"
#include "display/backlight.h"
#include "display/lgfx_panel.h"
#include "display/touch_gt911.h"

namespace {

constexpr const char* TAG = "display";

// Owns the RGB panel and the two full-screen PSRAM frame buffers LVGL draws into.
LGFX lcd;

uint32_t tick_get()
{
    return millis();
}

// Switch the panel to the rendered buffer. The is_last guard matters only in
// DIRECT mode, where flush is called once per changed area; harmless in FULL.
void display_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixel_map)
{
    (void)area;
    if(lv_display_flush_is_last(display)) {
        if(!lcd.bus.presentFrameBuffer(pixel_map)) {
            LOGW(TAG, "VSYNC frame switch timeout");
        }
    }
    lv_display_flush_ready(display);
}

void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    static bool was_pressed = false;
    static bool swallow = false;  // true while a wake-up touch is still down

    int16_t x = 0;
    int16_t y = 0;
    const bool pressed = touch_poll(&x, &y);

    if(pressed && !was_pressed) {
        LOGI(TAG, "touch down x=%d y=%d", x, y);
        if(!backlight_is_on()) {
            backlight_set(true);
            swallow = true;
        }
    }
    if(!pressed) swallow = false;
    was_pressed = pressed;

    if(pressed && !swallow) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
    }
    else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

}  // namespace

bool display_init()
{
    Wire1.begin(cfg::PIN_I2C_SDA, cfg::PIN_I2C_SCL);
    Wire1.setClock(cfg::I2C_FREQ_HZ);

    if(!lcd.begin()) {
        LOGE(TAG, "lcd.begin() failed");
        return false;
    }
    delay(200);

    // Elecrow's example restarts the I2C bus after the RGB panel starts.
    // Kept as-is; do not remove without re-testing touch.
    Wire1.end();
    delay(10);
    Wire1.begin(cfg::PIN_I2C_SDA, cfg::PIN_I2C_SCL);
    Wire1.setClock(cfg::I2C_FREQ_HZ);

    lv_init();
    lv_tick_set_cb(tick_get);

    if(!touch_init()) {
        LOGW(TAG, "touch controller did not answer; screen will draw but touch will not work");
    }

    lv_color_t *frame_buffer_0 = reinterpret_cast<lv_color_t *>(lcd.bus.getFrameBuffer(0));
    lv_color_t *frame_buffer_1 = reinterpret_cast<lv_color_t *>(lcd.bus.getFrameBuffer(1));
    LOGI(TAG, "RGB frame buffers: %p, %p", frame_buffer_0, frame_buffer_1);
    if(frame_buffer_0 == nullptr || frame_buffer_1 == nullptr) {
        LOGE(TAG, "RGB double frame buffer allocation failed");
        return false;
    }

    lv_display_t *display = lv_display_create(lcd.width(), lcd.height());
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, display_flush);
    lv_display_set_buffers(display, frame_buffer_0, frame_buffer_1,
                           cfg::SCREEN_W * cfg::SCREEN_H * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_FULL);

    lv_indev_t *touchpad = lv_indev_create();
    lv_indev_set_type(touchpad, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touchpad, touchpad_read);

    return true;
}

void display_run_flash_safe(void (*work)(void *), void *arg)
{
    const uint32_t started = millis();

    backlight_pause();
    work(arg);
    for(int frame = 0; frame < cfg::FLASH_SETTLE_FRAMES; ++frame) lcd.bus.waitVSync();
    backlight_resume();
    // Only mark the screen dirty. Rendering is left to the next lv_timer_handler
    // pass in the main loop: calling lv_refr_now() here runs a render from the
    // middle of app::tick(), while a deferred tile rebuild may still be queued.
    lv_obj_invalidate(lv_screen_active());

    LOGI(TAG, "flash write done; screen blanked for %lu ms", static_cast<unsigned long>(millis() - started));
}
