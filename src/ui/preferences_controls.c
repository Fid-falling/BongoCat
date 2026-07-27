#include "preferences_controls.h"
#include "ui_animation.h"
#include "ui_backend.h"
#include "ui_catime.h"
#include "ui_paint.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct RepeatState {
    struct nk_context *context;
    char id[64];
    int direction;
    uint64_t next_ns;
} RepeatState;

static RepeatState repeat_state;

static void centered(struct nk_command_buffer *canvas, struct nk_rect bounds,
    const char *text, const struct nk_user_font *font, struct nk_color color) {
    float width = font->width(font->userdata, font->height,
        text, nk_strlen(text));
    struct nk_rect target = nk_rect(bounds.x + (bounds.w - width) * .5f,
        bounds.y + (bounds.h - font->height) * .5f, width + 1, font->height);
    nk_draw_text(canvas, target, text, nk_strlen(text), font,
        nk_rgba(0, 0, 0, 0), color);
}

static bool clicked(struct nk_context *context, struct nk_rect bounds) {
    return nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, bounds) != 0;
}

static int repeat_direction(struct nk_context *context, const char *id,
    struct nk_rect minus, struct nk_rect plus) {
    bool down = nk_input_is_mouse_down(&context->input, NK_BUTTON_LEFT);
    bool same = repeat_state.context == context &&
        strcmp(repeat_state.id, id) == 0;
    if (!down) {
        if (repeat_state.context == context) memset(&repeat_state, 0,
            sizeof(repeat_state));
        return 0;
    }
    int clicked_direction = nk_input_is_mouse_click_down_in_rect(&context->input,
        NK_BUTTON_LEFT, minus, nk_true) ? -1 :
        (nk_input_is_mouse_click_down_in_rect(&context->input,
        NK_BUTTON_LEFT, plus, nk_true) ? 1 : 0);
    uint64_t now = SDL_GetTicksNS();
    if (clicked_direction && (!same || repeat_state.direction != clicked_direction)) {
        repeat_state.context = context;
        snprintf(repeat_state.id, sizeof(repeat_state.id), "%s", id);
        repeat_state.direction = clicked_direction;
        repeat_state.next_ns = now + 360000000ULL;
        return clicked_direction;
    }
    struct nk_rect active = repeat_state.direction < 0 ? minus : plus;
    if (!same || !nk_input_is_mouse_hovering_rect(&context->input, active) ||
        now < repeat_state.next_ns) return 0;
    repeat_state.next_ns = now + 60000000ULL;
    return repeat_state.direction;
}

bool bongo_cat_neo_pref_controls_animating(struct nk_context *context) {
    return repeat_state.context == context &&
        nk_input_is_mouse_down(&context->input, NK_BUTTON_LEFT);
}

static double stepper(struct nk_context *context, const char *id,
    double minimum, double value, double maximum, double step,
    bool integer, bool *changed) {
    struct nk_rect bounds;
    *changed = false;
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return value;
    bounds = nk_rect(bounds.x + bounds.w - 122.0f,
        bounds.y, 122.0f, bounds.h);
    BongoCatNeoUIPalette p = bongo_cat_neo_ui_palette(bongo_cat_neo_ui_dark(context));
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    struct nk_rect minus = nk_rect(bounds.x, bounds.y, 36, bounds.h);
    struct nk_rect plus = nk_rect(bounds.x + bounds.w - 36, bounds.y, 36, bounds.h);
    struct nk_rect number_box = nk_rect(bounds.x + 36, bounds.y, bounds.w - 72,
        bounds.h);
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, bounds);
    bool minus_hover = nk_input_is_mouse_hovering_rect(&context->input, minus);
    bool plus_hover = nk_input_is_mouse_hovering_rect(&context->input, plus);
    char hover_id[80], minus_id[80], plus_id[80];
    snprintf(hover_id, sizeof(hover_id), "stepper-hover-%s", id);
    snprintf(minus_id, sizeof(minus_id), "stepper-minus-%s", id);
    snprintf(plus_id, sizeof(plus_id), "stepper-plus-%s", id);
    float hover_amount = bongo_cat_neo_ui_animate_eased(context, hover_id,
        hover ? 1.0f : 0.0f, 200, BONGO_CAT_NEO_UI_EASE_STANDARD);
    float minus_amount = bongo_cat_neo_ui_animate_eased(context, minus_id,
        minus_hover && value > minimum ? 1.0f : 0.0f, 150,
        BONGO_CAT_NEO_UI_EASE_STANDARD);
    float plus_amount = bongo_cat_neo_ui_animate_eased(context, plus_id,
        plus_hover && value < maximum ? 1.0f : 0.0f, 150,
        BONGO_CAT_NEO_UI_EASE_STANDARD);
    nk_fill_rect(canvas, bounds, 11, p.field);
    if (minus_amount > 0) nk_fill_rect(canvas, minus, 10,
        bongo_cat_neo_ui_color_mix(p.field, p.accent, minus_amount));
    if (plus_amount > 0) nk_fill_rect(canvas, plus, 10,
        bongo_cat_neo_ui_color_mix(p.field, p.accent, plus_amount));
    nk_stroke_rect(canvas, bounds, 11, 1.0f,
        bongo_cat_neo_ui_color_mix(p.border_subtle, p.accent, hover_amount));
    struct nk_color minus_color = value <= minimum ? p.border_subtle :
        bongo_cat_neo_ui_color_mix(p.muted, nk_rgb(255, 255, 255), minus_amount);
    struct nk_color plus_color = value >= maximum ? p.border_subtle :
        bongo_cat_neo_ui_color_mix(p.muted, nk_rgb(255, 255, 255), plus_amount);
    float cy = bounds.y + bounds.h * .5f;
    nk_stroke_line(canvas, minus.x + 13, cy, minus.x + 27, cy, 2, minus_color);
    nk_stroke_line(canvas, plus.x + 13, cy, plus.x + 27, cy, 2, plus_color);
    nk_stroke_line(canvas, plus.x + 20, cy - 7,
        plus.x + 20, cy + 7, 2, plus_color);
    char number[32];
    if (integer) snprintf(number, sizeof(number), "%.0f", value);
    else if (step < .1) snprintf(number, sizeof(number), "%.2f", value);
    else if (step < 1.0) snprintf(number, sizeof(number), "%.1f", value);
    else snprintf(number, sizeof(number), "%.0f", value);
    centered(canvas, number_box, number, bongo_cat_neo_ui_body_font(context), p.text);
    if (hover) bongo_cat_neo_ui_cursor_hover_rect(context, bounds,
        BONGO_CAT_NEO_UI_CURSOR_POINTER);
    int direction = repeat_direction(context, id, minus, plus);
    float wheel = context->input.mouse.scroll_delta.y;
    if (nk_input_is_mouse_hovering_rect(&context->input, number_box) && wheel != 0) {
        direction = wheel > 0 ? 1 : -1;
        context->input.mouse.scroll_delta.y = 0;
    }
    double next = NK_CLAMP(minimum, value + direction * step, maximum);
    if (next != value) { value = next; *changed = true; }
    return value;
}

bool bongo_cat_neo_pref_control_float(struct nk_context *context, const char *id,
    float minimum, float *value, float maximum, float step) {
    bool changed;
    double result = stepper(context, id, minimum, *value, maximum, step,
        false, &changed);
    if (changed) *value = (float)result;
    return changed;
}

bool bongo_cat_neo_pref_control_int(struct nk_context *context, const char *id,
    int minimum, int *value, int maximum, int step) {
    bool changed;
    double result = stepper(context, id, minimum, *value, maximum, step,
        true, &changed);
    if (changed) *value = (int)result;
    return changed;
}

bool bongo_cat_neo_pref_control_slider(struct nk_context *context, const char *id,
    float minimum, float *value, float maximum, float step) {
    struct nk_rect bounds;
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return false;
    bounds = nk_rect(bounds.x + bounds.w - 220.0f,
        bounds.y + 2.0f, 220.0f, bounds.h - 4.0f);
    BongoCatNeoUIPalette p = bongo_cat_neo_ui_palette(bongo_cat_neo_ui_dark(context));
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    struct nk_rect value_box = nk_rect(bounds.x + bounds.w - 49,
        bounds.y, 49, bounds.h);
    struct nk_rect track = nk_rect(bounds.x + 2,
        bounds.y + bounds.h * .5f - 3, 154, 6);
    struct nk_rect hit = nk_rect(track.x - 3, bounds.y,
        track.w + 6, bounds.h);
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, hit);
    bool value_hover = nk_input_is_mouse_hovering_rect(&context->input, value_box);
    char hover_id[80];
    snprintf(hover_id, sizeof(hover_id), "slider-hover-%s", id);
    float hover_amount = bongo_cat_neo_ui_animate_eased(context, hover_id,
        hover ? 1.0f : 0.0f, 150, BONGO_CAT_NEO_UI_EASE_STANDARD);
    float before = *value;
    if (hover && nk_input_is_mouse_down(&context->input, NK_BUTTON_LEFT)) {
        float ratio = (context->input.mouse.pos.x - track.x) / track.w;
        ratio = NK_CLAMP(0.0f, ratio, 1.0f);
        float raw = minimum + ratio * (maximum - minimum);
        *value = minimum + roundf((raw - minimum) / step) * step;
        *value = NK_CLAMP(minimum, *value, maximum);
    }
    float wheel = context->input.mouse.scroll_delta.y;
    if ((hover || value_hover) && wheel != 0.0f) {
        *value = NK_CLAMP(minimum, *value + (wheel > 0 ? step : -step), maximum);
        context->input.mouse.scroll_delta.y = 0;
    }
    float ratio = (*value - minimum) / (maximum - minimum);
    nk_fill_rect(canvas, track, 3, p.field);
    nk_fill_rect(canvas, nk_rect(track.x, track.y, track.w * ratio, track.h),
        3, p.accent);
    float thumb_x = track.x + track.w * ratio;
    float thumb_width = 4.0f + 4.0f * hover_amount;
    struct nk_rect thumb = nk_rect(thumb_x - thumb_width * .5f,
        track.y - 9, thumb_width, 24);
    if (p.effects) bongo_cat_neo_ui_paint_shadow(context, thumb, 3,
        0, 1, 4.0f + 3.0f * hover_amount, 0,
        nk_rgba(p.accent.r, p.accent.g, p.accent.b, 89));
    nk_fill_rect(canvas, thumb, 3, p.accent);
    nk_fill_rect(canvas, value_box, 8, p.field);
    nk_stroke_rect(canvas, value_box, 8, 1, p.border_subtle);
    char number[24]; snprintf(number, sizeof(number), "%.0f%%", *value);
    centered(canvas, value_box, number, bongo_cat_neo_ui_body_font(context), p.text);
    if (hover || value_hover) bongo_cat_neo_ui_cursor_hover_rect(context,
        hover ? hit : value_box,
        BONGO_CAT_NEO_UI_CURSOR_POINTER);
    return before != *value;
}

static void combo_chevron(struct nk_command_buffer *canvas,
    struct nk_rect bounds, struct nk_color color, float open) {
    float x = bounds.x + bounds.w - 24, y = bounds.y + bounds.h * .5f;
    float angle = 3.14159265f * open, cosine = cosf(angle), sine = sinf(angle);
    float ax = -5 * cosine + 3 * sine, ay = -5 * sine - 3 * cosine;
    float bx = -2 * sine, by = 2 * cosine;
    float cx = 5 * cosine + 3 * sine, cy = 5 * sine - 3 * cosine;
    nk_stroke_line(canvas, x + ax, y + ay, x + bx, y + by, 2, color);
    nk_stroke_line(canvas, x + bx, y + by, x + cx, y + cy, 2, color);
}

static void combo_item(struct nk_context *context, struct nk_rect bounds,
    const char *label, bool selected, bool hover, BongoCatNeoUIPalette p) {
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    struct nk_rect selection = nk_rect(bounds.x + 6, bounds.y + 3,
        bounds.w - 12, bounds.h - 6);
    if (selected || hover) nk_fill_rect(canvas, selection, 9,
        selected ? p.selection : p.field);
    const struct nk_user_font *font = bongo_cat_neo_ui_body_font(context);
    struct nk_rect text = nk_rect(bounds.x + 12,
        bounds.y + (bounds.h - font->height) * .5f, bounds.w - 48, font->height);
    nk_draw_text(canvas, text, label, nk_strlen(label), font,
        nk_rgba(0, 0, 0, 0), selected ? p.accent : p.text);
    if (selected) {
        float x = bounds.x + bounds.w - 24, y = bounds.y + bounds.h * .5f;
        nk_stroke_line(canvas, x - 5, y, x - 1, y + 4, 2, p.accent);
        nk_stroke_line(canvas, x - 1, y + 4, x + 7, y - 5, 2, p.accent);
    }
}

int bongo_cat_neo_pref_control_combo(struct nk_context *context, const char *id,
    const char *const *items, int count, int selected) {
    if (count <= 0) return selected;
    selected = NK_CLAMP(0, selected, count - 1);
    BongoCatNeoUIPalette p = bongo_cat_neo_ui_palette(bongo_cat_neo_ui_dark(context));
    struct nk_rect bounds = nk_widget_bounds(context);
    struct nk_command_buffer *parent = nk_window_get_canvas(context);
    struct nk_style_combo saved_combo = context->style.combo;
    struct nk_style_window saved_window = context->style.window;
    context->style.combo.normal = nk_style_item_color(p.field);
    context->style.combo.hover = nk_style_item_color(p.hover);
    context->style.combo.active = nk_style_item_color(p.selection);
    context->style.combo.border_color = p.border_subtle;
    context->style.combo.label_normal = p.text;
    context->style.combo.label_hover = p.text;
    context->style.combo.label_active = p.accent;
    context->style.combo.sym_normal = context->style.combo.sym_hover =
        context->style.combo.sym_active = NK_SYMBOL_NONE;
    context->style.window.fixed_background = nk_style_item_color(p.surface);
    context->style.window.background = p.surface;
    context->style.window.border_color = p.border;
    context->style.window.border = 1;
    context->style.window.rounding = 12;
    context->style.window.padding = nk_vec2(8, 8);
    bool open = nk_combo_begin_label(context, items[selected],
        nk_vec2(bounds.w, NK_MIN(236.0f, 16.0f + count * 38.0f)));
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, bounds);
    char hover_id[80], open_id[80];
    snprintf(hover_id, sizeof(hover_id), "combo-hover-%s", id);
    snprintf(open_id, sizeof(open_id), "combo-open-%s", id);
    float hover_amount = bongo_cat_neo_ui_animate_eased(context, hover_id,
        open || hover ? 1.0f : 0.0f, 200, BONGO_CAT_NEO_UI_EASE_STANDARD);
    float open_amount = bongo_cat_neo_ui_animate_eased(context, open_id,
        open ? 1.0f : 0.0f, 160, BONGO_CAT_NEO_UI_EASE_STANDARD);
    nk_stroke_rect(parent, bounds, 10, 1.0f,
        bongo_cat_neo_ui_color_mix(p.border_subtle, p.accent, hover_amount));
    combo_chevron(parent, bounds,
        bongo_cat_neo_ui_color_mix(p.muted, p.accent, hover_amount), open_amount);
    if (hover) bongo_cat_neo_ui_cursor_hover_rect(context, bounds,
        BONGO_CAT_NEO_UI_CURSOR_POINTER);
    if (open) {
        nk_layout_row_dynamic(context, 38, 1);
        for (int i = 0; i < count; ++i) {
            struct nk_rect item;
            if (nk_widget(&item, context) == NK_WIDGET_INVALID) continue;
            bool item_hover = nk_input_is_mouse_hovering_rect(&context->input, item);
            combo_item(context, item, items[i], i == selected, item_hover, p);
            if (item_hover) bongo_cat_neo_ui_cursor_hover_rect(context, item,
                BONGO_CAT_NEO_UI_CURSOR_POINTER);
            if (item_hover && clicked(context, item)) {
                selected = i; nk_combo_close(context);
            }
        }
        nk_combo_end(context);
    }
    context->style.combo = saved_combo;
    context->style.window = saved_window;
    return selected;
}
