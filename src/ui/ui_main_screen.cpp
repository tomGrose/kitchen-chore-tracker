#include "ui/ui_main_screen.h"

#include <lvgl.h>
#include <string.h>

#include "config.h"

namespace {

// ---- layout (800x480) ----
constexpr int HEADER_H = 96;
constexpr int DEV_H = 40;
constexpr int GRID_PAD = 8;
constexpr int DETAILS_W = 620;      // details dialog card
constexpr int DETAILS_H = 410;
constexpr int DETAILS_PAD = 22;
constexpr int TILE_PAD = 10;
// Tiles shrink to fit when there are many, but never grow past this: filling
// the board with two enormous tiles reads as a mistake rather than a design.
constexpr int TILE_MAX_W = 250;
constexpr int TILE_MAX_H = 190;
constexpr int CHIPS_H = 54;     // strip of upcoming chores under the board
constexpr int CHIP_H = 40;
// The board gets whatever is left. Tiles are sized to fill it, so a day with
// two chores shows two big tiles rather than two small ones in a sea of space.
constexpr int GRID_H = cfg::SCREEN_H - HEADER_H - CHIPS_H - (cfg::DEV_TOOLS ? DEV_H : 0);

// Columns for a given number of tiles, chosen so the shapes stay sensible:
// four items read better as 2x2 than as one thin row.
int columns_for(int count)
{
    if(count <= 1) return 1;
    if(count == 2) return 2;
    if(count == 3) return 3;
    if(count == 4) return 2;
    if(count <= 6) return 3;
    return 4;
}

// ---- colours (spec section 5: red overdue, amber due, gray upcoming, green done) ----
constexpr uint32_t COLOR_SCREEN_BG = 0x0E1116;
constexpr uint32_t COLOR_TEXT = 0xFFFFFF;
constexpr uint32_t COLOR_TEXT_DIM = 0xA0A8B0;

struct TileColors {
    uint32_t bg;
    uint32_t text;
};

TileColors colors_for(UiTileState state)
{
    switch(state) {
    case UiTileState::Overdue:  return {0xC62828, 0xFFFFFF};
    case UiTileState::Due:      return {0xF9A825, 0x1B1B1B};
    case UiTileState::Upcoming: return {0x455A64, 0xE6ECEF};
    // Done tiles keep their place rather than moving, so they are recessed:
    // dark enough that the eye skips them while scanning for what is left,
    // still clearly green and legible up close.
    case UiTileState::Done:     return {0x1E3D22, 0x7FA886};
    // Skipped is not done: neutral grey, not green, so the board never implies
    // credit that was not earned.
    case UiTileState::Skipped:  return {0x222A31, 0x8A97A3};
    }
    return {0x455A64, 0xFFFFFF};
}

// ---- state ----
UiCallbacks s_cb;

lv_obj_t *s_clock = nullptr;
lv_obj_t *s_date = nullptr;
lv_obj_t *s_summary = nullptr;
lv_obj_t *s_wifi = nullptr;
lv_obj_t *s_grid = nullptr;
lv_obj_t *s_chips = nullptr;
lv_obj_t *s_empty = nullptr;
lv_obj_t *s_dev_label = nullptr;
lv_obj_t *s_overlay = nullptr;

UiHeader s_header;
std::vector<UiTile> s_tiles;        // latest data from the controller
std::vector<UiTile> s_shown;        // what is currently on screen
std::vector<UiChip> s_chip_data;
std::vector<UiChip> s_chips_shown;
bool s_chips_have_shown = false;
bool s_have_shown = false;
bool s_update_pending = false;
uint16_t s_pulse_id = 0;
bool s_nag = false;             // blink the attention tiles on the next rebuild

// A nag blinks discretely rather than animating: a smooth animation repaints
// every attention tile ~30 times a second, and eight tiles of that saturates
// PSRAM bandwidth and starves the panel (stale pixels down the left edge).
// Six opaque repaints cost almost nothing and read as more urgent anyway.
struct NagTile { lv_obj_t *obj; lv_color_t base; lv_color_t bright; };
std::vector<NagTile> s_nag_tiles;
lv_timer_t *s_nag_timer = nullptr;
int s_nag_steps_left = 0;
bool s_nag_lit = false;

void stop_nag_blink()
{
    if(s_nag_timer != nullptr) {
        lv_timer_delete(s_nag_timer);
        s_nag_timer = nullptr;
    }
    s_nag_tiles.clear();
    s_nag_steps_left = 0;
    s_nag_lit = false;
}

void nag_blink_cb(lv_timer_t *timer)
{
    (void)timer;
    s_nag_lit = !s_nag_lit;
    for(const NagTile &t : s_nag_tiles) {
        lv_obj_set_style_bg_color(t.obj, s_nag_lit ? t.bright : t.base, 0);
    }
    if(--s_nag_steps_left <= 0) {
        // Always land back on the base colour.
        for(const NagTile &t : s_nag_tiles) lv_obj_set_style_bg_color(t.obj, t.base, 0);
        stop_nag_blink();
    }
}
uint16_t s_details_id = 0;

bool tiles_equal(const std::vector<UiTile> &a, const std::vector<UiTile> &b)
{
    if(a.size() != b.size()) return false;
    for(size_t i = 0; i < a.size(); ++i) {
        if(a[i].id != b[i].id || a[i].state != b[i].state || a[i].streak != b[i].streak) return false;
        if(strcmp(a[i].name, b[i].name) != 0 || strcmp(a[i].status, b[i].status) != 0) return false;
    }
    return true;
}

void set_label_if_changed(lv_obj_t *label, const char *text)
{
    if(label != nullptr && strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

// ---- tile events ----
void on_tile_event(lv_event_t *e)
{
    const uint16_t id = static_cast<uint16_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    switch(lv_event_get_code(e)) {
    case LV_EVENT_SHORT_CLICKED:    // a plain tap (not a long press, not a scroll)
        if(s_cb.on_tile_tap != nullptr) s_cb.on_tile_tap(id);
        break;
    case LV_EVENT_LONG_PRESSED:
        if(s_cb.on_show_details != nullptr) s_cb.on_show_details(id);
        break;
    default:
        break;
    }
}

void scale_anim_cb(void *obj, int32_t value)
{
    lv_obj_set_style_transform_scale(static_cast<lv_obj_t *>(obj), value, 0);
}

void create_tile(const UiTile &t, bool pulse, bool nag, int tile_w, int tile_h)
{
    const TileColors colors = colors_for(t.state);

    lv_obj_t *tile = lv_obj_create(s_grid);
    lv_obj_set_size(tile, tile_w, tile_h);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);   // drags scroll the grid instead
    lv_obj_set_style_radius(tile, 14, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(colors.bg), 0);
    lv_obj_set_style_pad_all(tile, TILE_PAD, 0);
    lv_obj_set_style_transform_pivot_x(tile, tile_w / 2, 0);
    lv_obj_set_style_transform_pivot_y(tile, tile_h / 2, 0);
    lv_obj_set_style_transform_scale(tile, 240, LV_STATE_PRESSED);   // small press-down feedback

    const intptr_t user_data = static_cast<intptr_t>(t.id);
    lv_obj_add_event_cb(tile, on_tile_event, LV_EVENT_SHORT_CLICKED, reinterpret_cast<void *>(user_data));
    lv_obj_add_event_cb(tile, on_tile_event, LV_EVENT_LONG_PRESSED, reinterpret_cast<void *>(user_data));

    const int inner_w = tile_w - 2 * TILE_PAD;

    lv_obj_t *name = lv_label_create(tile);
    lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(name, inner_w);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(colors.text), 0);
    lv_label_set_text(name, t.name);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);

    const bool show_streak = t.streak >= 2;

    lv_obj_t *status = lv_label_create(tile);
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(status, show_streak ? inner_w - 44 : inner_w);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(colors.text), 0);
    lv_label_set_text(status, t.status);
    lv_obj_align(status, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    if(show_streak) {
        lv_obj_t *streak = lv_label_create(tile);
        lv_obj_set_style_text_font(streak, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(streak, lv_color_hex(colors.text), 0);
        lv_label_set_text_fmt(streak, "x%u", static_cast<unsigned>(t.streak));
        lv_obj_align(streak, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }

    if(pulse) {
        // Completion: a quick pop outwards. Means "done".
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, tile);
        lv_anim_set_values(&anim, 256, 290);
        lv_anim_set_duration(&anim, 140);
        lv_anim_set_playback_duration(&anim, 160);
        lv_anim_set_exec_cb(&anim, scale_anim_cb);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
        lv_anim_start(&anim);
    }
    else if(nag) {
        // Collected here, blinked by nag_blink_cb once the grid is built.
        NagTile entry;
        entry.obj = tile;
        entry.base = lv_color_hex(colors.bg);
        entry.bright = lv_color_mix(lv_color_hex(0xFFFFFF), entry.base, 140);
        s_nag_tiles.push_back(entry);
    }
}

void on_chip_click(lv_event_t *e)
{
    const uint16_t id = static_cast<uint16_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    // A tap opens the details rather than completing: doing an upcoming chore
    // early should be deliberate, not something a stray touch can do.
    if(s_cb.on_show_details != nullptr) s_cb.on_show_details(id);
}

bool chips_equal(const std::vector<UiChip> &a, const std::vector<UiChip> &b)
{
    if(a.size() != b.size()) return false;
    for(size_t i = 0; i < a.size(); ++i) {
        if(a[i].id != b[i].id || strcmp(a[i].text, b[i].text) != 0) return false;
    }
    return true;
}

void rebuild_chips()
{
    lv_obj_clean(s_chips);
    for(const UiChip &c : s_chip_data) {
        lv_obj_t *chip = lv_button_create(s_chips);
        lv_obj_set_height(chip, CHIP_H);
        lv_obj_set_width(chip, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(chip, CHIP_H / 2, 0);
        lv_obj_set_style_bg_color(chip, lv_color_hex(0x1C232C), 0);
        lv_obj_set_style_pad_hor(chip, 14, 0);
        lv_obj_set_style_shadow_width(chip, 0, 0);
        lv_obj_add_event_cb(chip, on_chip_click, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<uintptr_t>(c.id)));

        lv_obj_t *label = lv_label_create(chip);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT_DIM), 0);
        lv_label_set_text(label, c.text);
        lv_obj_center(label);
    }
    s_chips_shown = s_chip_data;
    s_chips_have_shown = true;
}

void rebuild_tiles()
{
    // lv_obj_clean deletes the tiles the blink timer holds pointers to.
    stop_nag_blink();

    const int32_t scroll_y = lv_obj_get_scroll_y(s_grid);
    lv_obj_clean(s_grid);

    const int count = static_cast<int>(s_tiles.size());
    if(count == 0) {
        lv_obj_add_flag(s_grid, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_remove_flag(s_grid, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);

        // Tiles fill the board. Two chores today means two big tiles, not two
        // small ones adrift in empty space.
        const int cols = columns_for(count);
        int rows = (count + cols - 1) / cols;
        if(rows > 2) rows = 2;   // more than fills the board: size for two and scroll

        // Fit to the space available, then cap. Tiles stay left-aligned rather
        // than centred so the first chore is always in the same corner, and so
        // completing something early (which appends a tile) does not shift the
        // ones already on screen.
        int tile_w = (cfg::SCREEN_W - 2 * GRID_PAD - (cols - 1) * GRID_PAD) / cols;
        int tile_h = (GRID_H - 2 * GRID_PAD - (rows - 1) * GRID_PAD) / rows;
        if(tile_w > TILE_MAX_W) tile_w = TILE_MAX_W;
        if(tile_h > TILE_MAX_H) tile_h = TILE_MAX_H;

        for(const UiTile &t : s_tiles) {
            const bool attention = t.state == UiTileState::Overdue || t.state == UiTileState::Due;
            create_tile(t, t.id == s_pulse_id, s_nag && attention, tile_w, tile_h);
        }
        lv_obj_update_layout(s_grid);
        lv_obj_scroll_to_y(s_grid, scroll_y, LV_ANIM_OFF);
    }

    if(!s_nag_tiles.empty()) {
        s_nag_steps_left = 6;          // three on/off cycles
        s_nag_lit = false;
        s_nag_timer = lv_timer_create(nag_blink_cb, 260, nullptr);
    }

    s_shown = s_tiles;
    s_have_shown = true;
    s_pulse_id = 0;
    s_nag = false;
}

// Runs from lv_timer_handler(), never from inside an event callback, so it can
// safely delete the tiles that triggered the update.
void apply_update_cb(void *)
{
    s_update_pending = false;

    set_label_if_changed(s_clock, s_header.time);
    set_label_if_changed(s_date, s_header.date);
    set_label_if_changed(s_summary, s_header.summary);
    set_label_if_changed(s_wifi, s_header.wifi);
    if(cfg::DEV_TOOLS) {
        set_label_if_changed(s_dev_label, s_header.dev);
    }

    if(!s_have_shown || s_pulse_id != 0 || s_nag || !tiles_equal(s_tiles, s_shown)) rebuild_tiles();
    if(!s_chips_have_shown || !chips_equal(s_chip_data, s_chips_shown)) rebuild_chips();
}

// ---- details dialog ----
void close_details()
{
    if(s_overlay == nullptr) return;
    lv_obj_delete_async(s_overlay);
    s_overlay = nullptr;
}

void on_overlay_click(lv_event_t *e)
{
    (void)e;
    close_details();
}

void on_mark_done_click(lv_event_t *e)
{
    (void)e;
    const uint16_t id = s_details_id;
    close_details();
    if(s_cb.on_mark_done != nullptr) s_cb.on_mark_done(id);
}

void on_skip_click(lv_event_t *e)
{
    (void)e;
    const uint16_t id = s_details_id;
    close_details();
    if(s_cb.on_skip != nullptr) s_cb.on_skip(id);
}

void on_snooze_click(lv_event_t *e)
{
    const int kind = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    const uint16_t id = s_details_id;
    close_details();
    if(s_cb.on_snooze != nullptr) s_cb.on_snooze(id, kind);
}

void on_close_click(lv_event_t *e)
{
    (void)e;
    close_details();
}

void on_undo_click(lv_event_t *e)
{
    (void)e;
    const uint16_t id = s_details_id;
    close_details();
    if(s_cb.on_undo != nullptr) s_cb.on_undo(id);
}

lv_obj_t *make_button(lv_obj_t *parent, const char *text, int width, int height, uint32_t color,
                      lv_event_cb_t cb, void *user_data = nullptr)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

// ---- dev strip (Phase 2 testing only) ----
void on_dev_shift(lv_event_t *e)
{
    const int delta = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if(s_cb.on_dev_shift_minutes != nullptr) s_cb.on_dev_shift_minutes(delta);
}

void on_dev_reset_click(lv_event_t *e)
{
    (void)e;
    if(s_cb.on_dev_reset != nullptr) s_cb.on_dev_reset();
}

void create_dev_strip(lv_obj_t *screen)
{
    lv_obj_t *strip = lv_obj_create(screen);
    lv_obj_set_size(strip, cfg::SCREEN_W, DEV_H);
    lv_obj_set_pos(strip, 0, cfg::SCREEN_H - DEV_H);
    lv_obj_remove_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(strip, 0, 0);
    lv_obj_set_style_border_width(strip, 0, 0);
    lv_obj_set_style_pad_all(strip, 4, 0);
    lv_obj_set_style_bg_color(strip, lv_color_hex(0x1A1F27), 0);

    struct DevButton { const char *text; int x; int width; int delta; bool reset; };
    const DevButton buttons[] = {
        {"-1 hr",  0,   100,   -60, false},
        {"+1 hr",  108, 100,   +60, false},
        {"-1 day", 216, 100, -1440, false},
        {"+1 day", 324, 100, +1440, false},
        {"Reset",  432, 100,     0, true},
    };
    for(const DevButton &b : buttons) {
        lv_obj_t *button = lv_button_create(strip);
        lv_obj_set_size(button, b.width, DEV_H - 8);
        lv_obj_set_pos(button, b.x, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x37474F), 0);
        if(b.reset) lv_obj_add_event_cb(button, on_dev_reset_click, LV_EVENT_CLICKED, nullptr);
        else lv_obj_add_event_cb(button, on_dev_shift, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(b.delta)));

        lv_obj_t *label = lv_label_create(button);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
        lv_label_set_text(label, b.text);
        lv_obj_center(label);
    }

    s_dev_label = lv_label_create(strip);
    lv_obj_set_style_text_font(s_dev_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_dev_label, lv_color_hex(COLOR_TEXT_DIM), 0);
    lv_label_set_text(s_dev_label, "");
    lv_obj_align(s_dev_label, LV_ALIGN_RIGHT_MID, 0, 0);
}

}  // namespace

void ui_main_create(const UiCallbacks &callbacks)
{
    s_cb = callbacks;

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_SCREEN_BG), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    s_clock = lv_label_create(screen);
    lv_obj_set_style_text_font(s_clock, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_clock, lv_color_hex(COLOR_TEXT), 0);
    lv_label_set_text(s_clock, "");
    lv_obj_set_pos(s_clock, 16, 4);

    s_date = lv_label_create(screen);
    lv_obj_set_style_text_font(s_date, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_date, lv_color_hex(COLOR_TEXT_DIM), 0);
    lv_label_set_text(s_date, "");
    lv_obj_set_pos(s_date, 18, 62);

    s_summary = lv_label_create(screen);
    lv_obj_set_style_text_font(s_summary, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(s_summary, lv_color_hex(COLOR_TEXT), 0);
    lv_label_set_text(s_summary, "");
    lv_obj_align(s_summary, LV_ALIGN_TOP_RIGHT, -16, 24);

    // Connection status: small, dim, and directly under the summary so a glance
    // from across the room is not drawn to it.
    s_wifi = lv_label_create(screen);
    lv_obj_set_style_text_font(s_wifi, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_wifi, lv_color_hex(COLOR_TEXT_DIM), 0);
    lv_label_set_text(s_wifi, "");
    lv_obj_align(s_wifi, LV_ALIGN_TOP_RIGHT, -16, 70);

    s_grid = lv_obj_create(screen);
    lv_obj_set_size(s_grid, cfg::SCREEN_W, GRID_H);
    lv_obj_set_pos(s_grid, 0, HEADER_H);
    lv_obj_set_style_bg_opa(s_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_grid, 0, 0);
    lv_obj_set_style_radius(s_grid, 0, 0);
    lv_obj_set_style_pad_all(s_grid, GRID_PAD, 0);
    lv_obj_set_style_pad_gap(s_grid, GRID_PAD, 0);
    lv_obj_set_flex_flow(s_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_scroll_dir(s_grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_grid, LV_SCROLLBAR_MODE_OFF);

    // Shown in place of the board when there is nothing left today.
    s_empty = lv_label_create(screen);
    lv_obj_set_style_text_font(s_empty, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(s_empty, lv_color_hex(COLOR_TEXT_DIM), 0);
    lv_label_set_text(s_empty, "Nothing left today");
    lv_obj_set_pos(s_empty, 0, HEADER_H);
    lv_obj_set_size(s_empty, cfg::SCREEN_W, GRID_H);
    lv_obj_set_style_text_align(s_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(s_empty, GRID_H / 2 - 28, 0);
    lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);

    // Chores not due today: visible at a glance, not competing with the board.
    s_chips = lv_obj_create(screen);
    lv_obj_set_size(s_chips, cfg::SCREEN_W, CHIPS_H);
    lv_obj_set_pos(s_chips, 0, HEADER_H + GRID_H);
    lv_obj_set_style_bg_opa(s_chips, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_chips, 0, 0);
    lv_obj_set_style_radius(s_chips, 0, 0);
    lv_obj_set_style_pad_all(s_chips, GRID_PAD, 0);
    lv_obj_set_style_pad_gap(s_chips, GRID_PAD, 0);
    lv_obj_set_flex_flow(s_chips, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(s_chips, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(s_chips, LV_SCROLLBAR_MODE_OFF);

    if(cfg::DEV_TOOLS) create_dev_strip(screen);
}

void ui_main_update(const UiHeader &header, const std::vector<UiTile> &tiles,
                    const std::vector<UiChip> &chips, uint16_t pulse_id, bool nag)
{
    s_header = header;
    s_tiles = tiles;
    s_chip_data = chips;
    if(pulse_id != 0) s_pulse_id = pulse_id;
    if(nag) s_nag = true;

    if(!s_update_pending) {
        s_update_pending = true;
        lv_async_call(apply_update_cb, nullptr);
    }
}

void ui_main_show_details(const UiDetails &d)
{
    if(s_overlay != nullptr) return;   // already open
    s_details_id = d.id;

    // Dimmed full-screen layer; tapping outside the card closes it.
    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_overlay, cfg::SCREEN_W, cfg::SCREEN_H);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_overlay, 0, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_60, 0);
    lv_obj_add_event_cb(s_overlay, on_overlay_click, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *card = lv_obj_create(s_overlay);
    lv_obj_set_size(card, DETAILS_W, DETAILS_H);
    lv_obj_center(card);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1E2530), 0);
    lv_obj_set_style_pad_all(card, DETAILS_PAD, 0);

    lv_obj_t *title = lv_label_create(card);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), 0);
    lv_label_set_text(title, d.name);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *lines = lv_label_create(card);
    lv_obj_set_width(lines, 550);
    lv_obj_set_style_text_font(lines, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lines, lv_color_hex(0xCFD8DC), 0);
    lv_obj_set_style_text_line_space(lines, 10, 0);
    lv_label_set_text_fmt(lines, "Schedule: %s\n%s\n%s", d.schedule, d.last, d.streak);
    lv_obj_align(lines, LV_ALIGN_TOP_LEFT, 0, 66);

    // Snooze row: only meaningful for something that is actually asking for
    // attention, so it is hidden entirely for upcoming and done chores.
    if(d.can_skip) {
        lv_obj_t *skip = make_button(card, d.is_skipped ? "Un-skip" : d.skip_label,
                                     200, 52, d.is_skipped ? 0x455A64 : 0x5D4037, on_skip_click);
        lv_obj_align(skip, LV_ALIGN_TOP_RIGHT, 0, 176);
    }

    if(d.can_snooze) {
        const bool snoozed = d.snooze[0] != '\0';

        lv_obj_t *snooze_label = lv_label_create(card);
        lv_obj_set_style_text_font(snooze_label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(snooze_label, lv_color_hex(COLOR_TEXT_DIM), 0);
        lv_label_set_text(snooze_label, snoozed ? d.snooze : "Snooze nagging for");
        lv_obj_align(snooze_label, LV_ALIGN_TOP_LEFT, 0, 190);

        // Laid out as one row across the card's content width, sized to fit
        // however many buttons there are, rather than each positioned by hand.
        struct SnoozeButton { const char *text; int kind; };
        SnoozeButton buttons[4];
        int count = 0;
        buttons[count++] = {"15 min", 0};
        buttons[count++] = {"1 hour", 1};
        buttons[count++] = {"Tomorrow", 2};
        if(snoozed) buttons[count++] = {"Cancel", 3};

        const int gap = 10;
        const int content_w = DETAILS_W - 2 * DETAILS_PAD;
        const int width = (content_w - gap * (count - 1)) / count;
        for(int i = 0; i < count; ++i) {
            lv_obj_t *button = make_button(card, buttons[i].text, width, 56, 0x37474F, on_snooze_click,
                                           reinterpret_cast<void *>(static_cast<intptr_t>(buttons[i].kind)));
            lv_obj_align(button, LV_ALIGN_TOP_LEFT, i * (width + gap), 240);
        }
    }

    // Bottom row, laid out across the content width like the snooze row above,
    // so adding a button never needs every position re-picking by hand.
    struct BottomButton { const char *text; uint32_t color; lv_event_cb_t cb; bool enabled; };
    BottomButton row[3];
    int row_count = 0;
    if(d.can_complete) row[row_count++] = {"Mark done", 0x2E7D32, on_mark_done_click, true};
    row[row_count++] = {"Undo", 0xB23A3A, on_undo_click, d.can_undo};
    row[row_count++] = {"Close", 0x37474F, on_close_click, true};

    const int bottom_gap = 12;
    const int bottom_w = (DETAILS_W - 2 * DETAILS_PAD - bottom_gap * (row_count - 1)) / row_count;
    for(int i = 0; i < row_count; ++i) {
        lv_obj_t *button = make_button(card, row[i].text, bottom_w, 60, row[i].color, row[i].cb);
        lv_obj_align(button, LV_ALIGN_BOTTOM_LEFT, i * (bottom_w + bottom_gap), 0);
        if(!row[i].enabled) lv_obj_add_state(button, LV_STATE_DISABLED);
    }
}
