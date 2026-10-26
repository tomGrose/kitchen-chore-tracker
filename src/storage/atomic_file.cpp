#include "storage/atomic_file.h"

#include <Arduino.h>

#include "app_log.h"

namespace {
constexpr const char *TAG = "file";
}

namespace atomicfile {

bool readAll(fs::FS &fs, const char *path, std::string &out)
{
    File file = fs.open(path, "r");
    if(!file) return false;
    const size_t size = file.size();
    out.assign(size, '\0');
    const size_t got = size > 0 ? file.read(reinterpret_cast<uint8_t *>(&out[0]), size) : 0;
    file.close();
    return got == size;
}

bool write(fs::FS &fs, const char *path, const char *tmpPath, const void *data, size_t length)
{
    File file = fs.open(tmpPath, "w");
    if(!file) {
        LOGE(TAG, "cannot open %s for writing", tmpPath);
        return false;
    }
    const size_t written = file.write(static_cast<const uint8_t *>(data), length);
    file.close();
    if(written != length) {
        LOGE(TAG, "short write to %s (%u of %u bytes)", tmpPath,
             static_cast<unsigned>(written), static_cast<unsigned>(length));
        fs.remove(tmpPath);
        return false;
    }

    // Between this remove and the rename the complete data sits in tmpPath;
    // recoverTmp() finds it if power fails right here.
    fs.remove(path);
    if(!fs.rename(tmpPath, path)) {
        LOGE(TAG, "rename %s -> %s failed", tmpPath, path);
        return false;
    }
    return true;
}

bool recoverTmp(fs::FS &fs, const char *path, const char *tmpPath)
{
    if(!fs.exists(tmpPath)) return false;
    LOGW(TAG, "recovering %s from %s after an interrupted write", path, tmpPath);
    fs.remove(path);
    return fs.rename(tmpPath, path);
}

void removeAll(fs::FS &fs, const char *path, const char *tmpPath)
{
    fs.remove(path);
    fs.remove(tmpPath);
}

}  // namespace atomicfile
