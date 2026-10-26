#pragma once
/*---------------------------------------------------------------
 * Read a whole file, and replace one without ever leaving a half-written
 * file behind: write <path>.tmp, remove <path>, rename. If power is cut in
 * the window between the remove and the rename, the complete data is still
 * in the .tmp file and recoverTmp() promotes it on the next boot.
 *--------------------------------------------------------------*/
#include <FS.h>

#include <string>

namespace atomicfile {

bool readAll(fs::FS &fs, const char *path, std::string &out);

// tmpPath must be on the same filesystem as path.
bool write(fs::FS &fs, const char *path, const char *tmpPath, const void *data, size_t length);

// Promote a leftover tmp file from an interrupted write. True if one was found.
bool recoverTmp(fs::FS &fs, const char *path, const char *tmpPath);

void removeAll(fs::FS &fs, const char *path, const char *tmpPath);

}  // namespace atomicfile
