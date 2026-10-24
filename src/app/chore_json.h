#pragma once
/*---------------------------------------------------------------
 * JSON (de)serialisation of all chore data, schema version 1.
 * Dates are stored as day numbers (days since 1970-01-01) to keep the
 * file small; Phase 4's export/import can convert to ISO dates.
 * Needs ArduinoJson 7 only; no Arduino dependency.
 *--------------------------------------------------------------*/
#include <stddef.h>

#include <string>

#include "app/chore_types.h"

namespace chorejson {

std::string serialize(const ChoreData &data);

// Returns false (and leaves `out` untouched) if the text is not valid
// version-1 chore data.
bool parse(const char *json, size_t length, ChoreData &out);

}  // namespace chorejson
