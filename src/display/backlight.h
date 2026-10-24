#pragma once

void backlight_init(bool on);
void backlight_set(bool on);
bool backlight_is_on();

// Temporarily force the backlight pin off/back to its logical state. Used to hide
// the display while flash is being written (see display_run_flash_safe).
void backlight_pause();
void backlight_resume();
