#pragma once
/*---------------------------------------------------------------
 * Chore data persistence (spec sections 3 and 4).
 * Files:  /chores.json  current data
 *         /chores.tmp   being written / awaiting rename
 *         /chores.bad   a file that failed to parse, kept for inspection
 *
 * Storage backend and atomic-write mechanics live in storage/fs_backend and
 * storage/atomic_file. fsbackend::begin() must have been called first.
 *--------------------------------------------------------------*/
#include "app/chore_types.h"

namespace chore_store {

// false: no valid data stored; `out` is left untouched.
bool load(ChoreData &out);

bool save(const ChoreData &data);

// Delete all stored chore files.
void wipe();

}  // namespace chore_store
