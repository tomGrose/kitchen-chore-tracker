#include "storage/fs_backend.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>

#include "app_log.h"
#include "config.h"

namespace {

constexpr const char *TAG = "fs";

fs::FS *g_fs = nullptr;
fsbackend::Backend g_backend = fsbackend::Backend::None;

// The SD slot has its own SPI bus; the RGB panel uses the LCD peripheral and
// touch uses I2C, so nothing else contends for these pins.
SPIClass g_sd_spi(FSPI);

bool mount_sd()
{
    g_sd_spi.begin(cfg::PIN_SD_CLK, cfg::PIN_SD_MISO, cfg::PIN_SD_MOSI, cfg::PIN_SD_CS);
    if(!SD.begin(cfg::PIN_SD_CS, g_sd_spi, cfg::SD_FREQ_HZ)) {
        g_sd_spi.end();
        return false;
    }
    if(SD.cardType() == CARD_NONE) {
        SD.end();
        g_sd_spi.end();
        return false;
    }
    LOGI(TAG, "SD card mounted: %lu MB used of %lu MB",
         static_cast<unsigned long>(SD.usedBytes() / (1024UL * 1024UL)),
         static_cast<unsigned long>(SD.totalBytes() / (1024UL * 1024UL)));
    return true;
}

}  // namespace

namespace fsbackend {

bool begin()
{
    if(g_backend != Backend::None) return true;

    if(mount_sd()) {
        g_fs = &SD;
        g_backend = Backend::SdCard;
        return true;
    }

    LOGW(TAG, "no usable SD card; falling back to internal flash");
    // true = format the partition if it cannot be mounted (first boot after flashing).
    if(!LittleFS.begin(true)) {
        LOGE(TAG, "LittleFS mount failed too; nothing will persist");
        return false;
    }
    LOGW(TAG, "LittleFS mounted: %u KB used of %u KB; writes will blank the screen",
         static_cast<unsigned>(LittleFS.usedBytes() / 1024),
         static_cast<unsigned>(LittleFS.totalBytes() / 1024));
    g_fs = &LittleFS;
    g_backend = Backend::Flash;
    return true;
}

fs::FS *fs()
{
    return g_fs;
}

Backend backend()
{
    return g_backend;
}

const char *name()
{
    switch(g_backend) {
    case Backend::SdCard: return "SD card";
    case Backend::Flash:  return "internal flash";
    case Backend::None:   break;
    }
    return "none";
}

bool writesInternalFlash()
{
    return g_backend == Backend::Flash;
}

}  // namespace fsbackend
