#pragma once
/*---------------------------------------------------------------
 * Glue between the chore engine, storage, clock, and the main screen.
 * This is the only place that knows about all of them.
 *--------------------------------------------------------------*/

namespace app {

// Mount storage, load (or seed) the chores, build the main screen.
// Call after display_init().
void init();

// Call from loop(). Cheap: refreshes the screen when the clock minute or day
// changes. Not blocking.
void tick();

}  // namespace app
