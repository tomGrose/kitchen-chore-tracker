#include "ui/ui_main_screen.h"

#include <lvgl.h>
#include <string.h>

#include "config.h"

namespace {

// ---- layout (800x480) ----
constexpr int HEADER_H = 96;
constexpr int DEV_H = 40;
constexpr int GRID_PAD = 8;
constexpr int TILE_W = 190;     // 4 tiles + 3 gaps of 8 fit the 784 px inner width exactly
constexpr int TILE_H = 160;
constexpr int TILE_PAD = 10;
constexpr int GRID_H = cfg::SCREEN_H - HEADER_H - (cfg::DEV_TOOLS ? DEV_H : 0);

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
    case UiTileState::Done:     return {0x2E7D32, 0xFFFFFF};
    }
    return {0x455A64, 0xFFFFFF};
}

// ---- state ----
UiCallbacks s_cb;

lv_obj_t *s_clock = nullptr;
lv_obj_t *s_date = nullptr;
lv_obj_t *s_summary = nullptr;
lv_obj_t *s_grid = nullptr;
lv_obj_t *s_dev_label = nullptr;
lv_obj_t *s_dev_anim_label = nullptr;
lv_obj_t *s_dev_save_label = nullptr;
lv_obj_t *s_overlay = nullptr;

UiHeader s_header;
std::vector<UiTile> s_tiles;        // latest data from the controller
std::vector<UiTile> s_shown;        // what is currently on screen
bool s_have_shown = false;
bool s_update_pending = false;
uint16_t s_pulse_id = 0;
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
        if(s_cb.on_tile_long_press != nullptr) s_cb.on_tile_long_press(id);
        break;
    default:
        break;
    }
}

void scale_anim_cb(void *obj, int32_t value)
{
    lv_obj_set_style_transform_scale(static_cast<lv_obj_t *>(obj), value, 0);
}

void create_tile(const UiTile &t, bool pulse)
{
    const TileColors colors = colors_for(t.state);

    lv_obj_t *tile = lv_obj_create(s_grid);
    lv_obj_set_size(tile, TILE_W, TILE_H);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);   // drags scroll the grid instead
    lv_obj_set_style_radius(tile, 14, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(colors.bg), 0);
    lv_obj_set_style_pad_all(tile, TILE_PAD, 0);
    lv_obj_set_style_transform_pivot_x(tile, TILE_W / 2, 0);
    lv_obj_set_style_transform_pivot_y(tile, TILE_H / 2, 0);
    lv_obj_set_style_transform_scale(tile, 240, LV_STATE_PRESSED);   // small press-down feedback

    const intptr_t user_data = static_cast<intptr_t>(t.id);
    lv_obj_add_event_cb(tile, on_tile_event, LV_EVENT_SHORT_CLICKED, reinterpret_cast<void *>(user_data));
    lv_obj_add_event_cb(tile, on_tile_event, LV_EVENT_LONG_PRESSED, reinterpret_cast<void *>(user_data));

    const int inner_w = TILE_W - 2 * TILE_PAD;

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
}

void rebuild_tiles()
{
    const int32_t scroll_y = lv_obj_get_scroll_y(s_grid);
    lv_obj_clean(s_grid);
    for(const UiTile &t : s_tiles) create_tile(t, t.id == s_pulse_id);
    lv_obj_update_layout(s_grid);
    lv_obj_scroll_to_y(s_grid, scroll_y, LV_ANIM_OFF);

    s_shown = s_tiles;
    s_have_shown = true;
    s_pulse_id = 0;
}

// Runs from lv_timer_handler(), never from inside an event callback, so it can
// safely delete the tiles that triggered the update.
void apply_update_cb(void *)
{
    s_update_pending = false;

    set_label_if_changed(s_clock, s_header.time);
    set_label_if_changed(s_date, s_header.date);
    set_label_if_changed(s_summary, s_header.summary);
    if(cfg::DEV_TOOLS) {
        set_label_if_changed(s_dev_label, s_header.dev);
        set_label_if_changed(s_dev_anim_label, s_header.dev_anim ? "Anim ON" : "Anim OFF");
        set_label_if_changed(s_dev_save_label, s_header.dev_save ? "Save ON" : "Save OFF");
    }

    if(!s_have_shown || s_pulse_id != 0 || !tiles_equal(s_tiles, s_shown)) rebuild_tiles();
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

lv_obj_t *make_button(lv_obj_t *parent, const char *text, int width, int height, uint32_t color, lv_event_cb_t cb)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, nullptr);

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
    if(s_cb.on_dev_shift_days != nullptr) s_cb.on_dev_shift_days(delta);
}

void on_dev_reset_click(lv_event_t *e)
{
    (void)e;
    if(s_cb.on_dev_reset != nullptr) s_cb.on_dev_reset();
}

void on_dev_toggle_click(lv_event_t *e)
{
    const int which = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if(s_cb.on_dev_toggle != nullptr) s_cb.on_dev_toggle(which);
}

lv_obj_t *make_dev_toggle(lv_obj_t *strip, int x, int which)
{
    lv_obj_t *button = lv_button_create(strip);
    lv_obj_set_size(button, 130, DEV_H - 8);
    lv_obj_set_pos(button, x, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x37474F), 0);
    lv_obj_add_event_cb(button, on_dev_toggle_click, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(which)));

    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_label_set_text(label, "");
    lv_obj_center(label);
    return label;
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
        {"-1 day", 0, 110, -1, false},
        {"+1 day", 118, 110, +1, false},
        {"Reset", 236, 110, 0, true},
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

    s_dev_anim_label = make_dev_toggle(strip, 354, 0);
    s_dev_save_label = make_dev_toggle(strip, 492, 1);

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

    if(cfg::DEV_TOOLS) create_dev_strip(screen);
}

void ui_main_update(const UiHeader &header, const std::vector<UiTile> &tiles, uint16_t pulse_id)
{
    s_header = header;
    s_tiles = tiles;
    if(pulse_id != 0) s_pulse_id = pulse_id;

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
    lv_obj_set_size(card, 600, 330);
    lv_obj_center(card);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1E2530), 0);
    lv_obj_set_style_pad_all(card, 22, 0);

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

    lv_obj_t *undo = make_button(card, "Undo last completion", 320, 60, 0xB23A3A, on_undo_click);
    lv_obj_align(undo, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    if(!d.can_undo) lv_obj_add_state(undo, LV_STATE_DISABLED);

    lv_obj_t *close = make_button(card, "Close", 190, 60, 0x37474F, on_close_click);
    lv_obj_align(close, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}
