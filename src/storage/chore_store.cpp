#include "storage/chore_store.h"

#include <Arduino.h>

#include <string>

#include "app/chore_json.h"
#include "app_log.h"
#include "storage/atomic_file.h"
#include "storage/fs_backend.h"

namespace {

constexpr const char *TAG = "chores";
constexpr const char *FILE_MAIN = "/chores.json";
constexpr const char *FILE_TMP = "/chores.tmp";
constexpr const char *FILE_BAD = "/chores.bad";

bool try_parse(fs::FS &fs, const char *path, ChoreData &out)
{
    if(!fs.exists(path)) return false;
    std::string text;
    if(!atomicfile::readAll(fs, path, text)) return false;
    return chorejson::parse(text.c_str(), text.size(), out);
}

}  // namespace

namespace chore_store {

bool load(ChoreData &out)
{
    fs::FS *filesystem = fsbackend::fs();
    if(filesystem == nullptr) return false;

    if(try_parse(*filesystem, FILE_MAIN, out)) {
        LOGI(TAG, "loaded %u chores, %u history entries from %s",
             static_cast<unsigned>(out.chores.size()), static_cast<unsigned>(out.history.size()),
             fsbackend::name());
        return true;
    }

    if(filesystem->exists(FILE_MAIN)) {
        LOGE(TAG, "%s is unreadable; moved to %s", FILE_MAIN, FILE_BAD);
        filesystem->remove(FILE_BAD);
        filesystem->rename(FILE_MAIN, FILE_BAD);
    }

    if(atomicfile::recoverTmp(*filesystem, FILE_MAIN, FILE_TMP) && try_parse(*filesystem, FILE_MAIN, out)) {
        return true;
    }
    return false;
}

bool save(const ChoreData &data)
{
    fs::FS *filesystem = fsbackend::fs();
    if(filesystem == nullptr) return false;

    const std::string json = chorejson::serialize(data);
    if(!atomicfile::write(*filesystem, FILE_MAIN, FILE_TMP, json.data(), json.size())) return false;
    LOGD(TAG, "saved %u bytes", static_cast<unsigned>(json.size()));
    return true;
}

void wipe()
{
    fs::FS *filesystem = fsbackend::fs();
    if(filesystem == nullptr) return;
    atomicfile::removeAll(*filesystem, FILE_MAIN, FILE_TMP);
    filesystem->remove(FILE_BAD);
}

}  // namespace chore_store
