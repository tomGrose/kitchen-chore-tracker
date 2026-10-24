#pragma once

// Bring up I2C, the RGB panel, LVGL, the double frame buffers, and touch
// (including the V3.0 PCA9557 reset). Returns false if the panel or its
// frame buffers cannot be created.
// Touching a dark screen wakes the backlight and that touch is not passed to
// the UI, so waking never presses a button by accident.
bool display_init();

// Run `work` (anything that writes flash: LittleFS, NVS) without visible screen
// glitches. A flash write briefly cuts the RGB panel off from PSRAM, which shows
// as lines/artifacts. This blanks the backlight, runs the work, lets a few frames
// scan out, redraws the whole screen, then restores the backlight.
// Call from the LVGL task (the Arduino loop). Blocks for the length of the write
// plus about 100 ms.
void display_run_flash_safe(void (*work)(void *), void *arg);
