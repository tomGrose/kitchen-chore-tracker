#include "storage/chore_store.h"

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>

#include <string>

#include "app/chore_json.h"
#include "app_log.h"
#include "config.h"

namespace {

constexpr const char *TAG = "store";
constexpr const char *FILE_MAIN = "/chores.json";
constexpr const char *FILE_TMP = "/chores.tmp";
constexpr const char *FILE_BAD = "/chores.bad";

// Whichever filesystem begin() settled on. Both SD and LittleFS derive from
// fs::FS, so everything below is backend-agnostic.
fs::FS *g_fs = nullptr;
chore_store::Backend g_backend = chore_store::Backend::None;

// The SD slot has its own SPI bus; the RGB panel uses the LCD peripheral and
// touch uses I2C, so nothing else contends for these pins.
SPIClass g_sd_spi(FSPI);

bool read_all(const char *path, std::string &out)
{
    File file = g_fs->open(path, "r");
    if(!file) return false;
    const size_t size = file.size();
    out.assign(size, '\0');
    const size_t got = size > 0 ? file.read(reinterpret_cast<uint8_t *>(&out[0]), size) : 0;
    file.close();
    return got == size;
}

bool try_parse(const char *path, ChoreData &out)
{
    if(!g_fs->exists(path)) return false;
    std::string text;
    if(!read_all(path, text)) return false;
    return chorejson::parse(text.c_str(), text.size(), out);
}

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

namespace chore_store {

bool begin()
{
    if(mount_sd()) {
        g_fs = &SD;
        g_backend = Backend::SdCard;
        return true;
    }

    LOGW(TAG, "no usable SD card; falling back to internal flash");
    // true = format the partition if it cannot be mounted (first boot after flashing).
    if(!LittleFS.begin(true)) {
        LOGE(TAG, "LittleFS mount failed too; chores will not persist");
        g_backend = Backend::None;
        return false;
    }
    LOGW(TAG, "LittleFS mounted: %u KB used of %u KB; saves will blank the screen",
         static_cast<unsigned>(LittleFS.usedBytes() / 1024),
         static_cast<unsigned>(LittleFS.totalBytes() / 1024));
    g_fs = &LittleFS;
    g_backend = Backend::Flash;
    return true;
}

Backend backend()
{
    return g_backend;
}

const char *backendName()
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

bool load(ChoreData &out)
{
    if(g_fs == nullptr) return false;

    if(try_parse(FILE_MAIN, out)) {
        LOGI(TAG, "loaded %u chores, %u history entries from %s",
             static_cast<unsigned>(out.chores.size()), static_cast<unsigned>(out.history.size()),
             backendName());
        return true;
    }

    if(g_fs->exists(FILE_MAIN)) {
        LOGE(TAG, "%s is unreadable; moved to %s", FILE_MAIN, FILE_BAD);
        g_fs->remove(FILE_BAD);
        g_fs->rename(FILE_MAIN, FILE_BAD);
    }

    if(try_parse(FILE_TMP, out)) {
        LOGW(TAG, "recovered data from %s after an interrupted save", FILE_TMP);
        g_fs->rename(FILE_TMP, FILE_MAIN);
        return true;
    }
    return false;
}

bool save(const ChoreData &data)
{
    if(g_fs == nullptr) return false;

    const std::string json = chorejson::serialize(data);

    File file = g_fs->open(FILE_TMP, "w");
    if(!file) {
        LOGE(TAG, "cannot open %s for writing", FILE_TMP);
        return false;
    }
    const size_t written = file.write(reinterpret_cast<const uint8_t *>(json.data()), json.size());
    file.close();
    if(written != json.size()) {
        LOGE(TAG, "short write (%u of %u bytes)", static_cast<unsigned>(written), static_cast<unsigned>(json.size()));
        g_fs->remove(FILE_TMP);
        return false;
    }

    // Between this remove and the rename the complete data sits in FILE_TMP;
    // load() recovers it if power fails right here.
    g_fs->remove(FILE_MAIN);
    if(!g_fs->rename(FILE_TMP, FILE_MAIN)) {
        LOGE(TAG, "rename %s -> %s failed", FILE_TMP, FILE_MAIN);
        return false;
    }
    LOGD(TAG, "saved %u bytes", static_cast<unsigned>(json.size()));
    return true;
}

void wipe()
{
    if(g_fs == nullptr) return;
    g_fs->remove(FILE_MAIN);
    g_fs->remove(FILE_TMP);
    g_fs->remove(FILE_BAD);
}

}  // namespace chore_store
