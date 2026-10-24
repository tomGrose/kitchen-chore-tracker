#pragma once
#include <stdint.h>

#include "app/chore_types.h"

// The household's real chore list from chores.md, used as hard-coded test
// data until the web page (Phase 4) can edit it. Every chore starts counting
// from `startDay`, so nothing is falsely overdue on first boot.
ChoreData seedChoreData(int32_t startDay);
