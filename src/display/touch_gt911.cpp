#include "display/touch_gt911.h"

#include <Arduino.h>
#include <Wire.h>

#include "app_log.h"
#include "config.h"

namespace {

constexpr const char* TAG = "touch";

// Addresses and registers: Elecrow V3.0 PlatformIO example (touch.h) and
// hardware-reference.md (PCA9557 touch timing control, V3.0 only).
constexpr uint8_t  PCA9557_ADDRESS = 0x18;
constexpr uint8_t  PCA9557_OUTPUT = 0x01;
constexpr uint8_t  PCA9557_CONFIG = 0x03;
constexpr uint8_t  GT911_ADDRESS = 0x5D;
constexpr uint16_t GT911_POINT_INFO = 0x814E;
constexpr uint16_t GT911_FIRST_POINT = 0x814F;

bool    s_pressed = false;
int16_t s_x = 0;
int16_t s_y = 0;

void pca9557_write(uint8_t reg, uint8_t value)
{
    Wire1.beginTransmission(PCA9557_ADDRESS);
    Wire1.write(reg);
    Wire1.write(value);
    Wire1.endTransmission();
}

uint8_t pca9557_read(uint8_t reg)
{
    Wire1.beginTransmission(PCA9557_ADDRESS);
    Wire1.write(reg);
    Wire1.endTransmission();
    Wire1.requestFrom(PCA9557_ADDRESS, static_cast<uint8_t>(1));
    return Wire1.read();
}

// input=true makes the pin an input (high impedance); false makes it an output.
void pca9557_set_mode(uint8_t pin, bool input)
{
    uint8_t config = pca9557_read(PCA9557_CONFIG);
    config = input ? config | (1U << pin) : config & ~(1U << pin);
    pca9557_write(PCA9557_CONFIG, config);
}

void pca9557_set_state(uint8_t pin, bool high)
{
    uint8_t output = pca9557_read(PCA9557_OUTPUT);
    output = high ? output | (1U << pin) : output & ~(1U << pin);
    pca9557_write(PCA9557_OUTPUT, output);
}

bool i2c_probe(uint8_t address)
{
    Wire1.beginTransmission(address);
    return Wire1.endTransmission() == 0;
}

bool gt911_read(uint16_t reg, uint8_t *data, size_t size)
{
    Wire1.beginTransmission(GT911_ADDRESS);
    Wire1.write(static_cast<uint8_t>(reg >> 8));
    Wire1.write(static_cast<uint8_t>(reg));
    if(Wire1.endTransmission(false) != 0) return false;
    if(Wire1.requestFrom(GT911_ADDRESS, size) != size) return false;

    for(size_t index = 0; index < size; ++index) data[index] = Wire1.read();
    return true;
}

void gt911_clear_status()
{
    Wire1.beginTransmission(GT911_ADDRESS);
    Wire1.write(static_cast<uint8_t>(GT911_POINT_INFO >> 8));
    Wire1.write(static_cast<uint8_t>(GT911_POINT_INFO));
    Wire1.write(0);
    Wire1.endTransmission();
}

int clamp_int(int value, int low, int high)
{
    if(value < low) return low;
    if(value > high) return high;
    return value;
}

// Read one sample. Leaves the cached state alone if the GT911 has no new data.
void sample_gt911()
{
    uint8_t status = 0;
    if(!gt911_read(GT911_POINT_INFO, &status, 1)) return;
    if((status & 0x80) == 0) return;  // no new data yet

    const uint8_t point_count = status & 0x0F;
    s_pressed = point_count > 0 && point_count <= 5;
    if(s_pressed) {
        uint8_t point[7];
        if(gt911_read(GT911_FIRST_POINT, point, sizeof(point))) {
            const int raw_x = point[1] | (static_cast<int>(point[2]) << 8);
            const int raw_y = point[3] | (static_cast<int>(point[4]) << 8);
            s_x = static_cast<int16_t>(clamp_int(raw_x, 0, cfg::SCREEN_W - 1));
            s_y = static_cast<int16_t>(clamp_int(raw_y, 0, cfg::SCREEN_H - 1));
        }
    }

    gt911_clear_status();
}

}  // namespace

bool touch_init()
{
    const bool pca_ok = i2c_probe(PCA9557_ADDRESS);
    LOGI(TAG, "PCA9557 @0x%02X: %s", PCA9557_ADDRESS, pca_ok ? "found" : "NOT FOUND");

    // V3.0 reset sequence from Elecrow (hardware-reference.md).
    pca9557_write(PCA9557_CONFIG, 0xFF);
    pca9557_set_mode(0, false);
    pca9557_set_mode(1, false);
    pca9557_set_state(0, false);
    pca9557_set_state(1, false);
    delay(20);
    pca9557_set_state(0, true);
    delay(100);
    pca9557_set_mode(1, true);

    const bool gt_ok = i2c_probe(GT911_ADDRESS);
    LOGI(TAG, "GT911   @0x%02X: %s", GT911_ADDRESS, gt_ok ? "found" : "NOT FOUND");
    return pca_ok && gt_ok;
}

bool touch_poll(int16_t* x, int16_t* y)
{
    static uint32_t last_sample = 0;
    const uint32_t now = millis();
    if(now - last_sample >= cfg::TOUCH_POLL_MS) {
        last_sample = now;
        sample_gt911();
    }

    if(s_pressed) {
        *x = s_x;
        *y = s_y;
    }
    return s_pressed;
}
