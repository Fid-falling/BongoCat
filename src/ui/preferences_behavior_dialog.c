#include "preferences_state.h"
#include "ui_backend.h"
#include "ui_catime.h"

#include <stdio.h>
#include <string.h>

bool bongo_cat_neo_preferences_behavior_dialog_active(
    const BongoCatNeoPreferences *value) {
    return value && value->behavior_dialog;
}

void bongo_cat_neo_preferences_behavior_dialog_close(
    BongoCatNeoPreferences *value) {
    if (!value) return;
    value->behavior_dialog = false;
    bongo_cat_neo_preferences_shortcut_cancel(value);
}

static const char *tr(BongoCatNeoPreferences *value, const char *key,
    const char *fallback) {
    return bongo_cat_neo_i18n_get(value->app->i18n, key, fallback);
}

static void text(struct nk_context *context, struct nk_command_buffer *canvas,
    struct nk_rect bounds, const char *value, const struct nk_user_font *font,
    struct nk_color color) {
    (void)context;
    nk_draw_text(canvas, bounds, value, nk_strlen(value), font,
        nk_rgba(0, 0, 0, 0), color);
}

static bool hit(struct nk_context *context, struct nk_rect bounds) {
    return nk_input_is_mouse_hovering_rect(&context->input, bounds) &&
        nk_input_is_mouse_click_in_rect(&context->input, NK_BUTTON_LEFT, bounds);
}

static BongoCatNeoBehaviorShortcut *shortcut_for(BongoCatNeoConfig *config,
    const char *id) {
    for (size_t i = 0; i < config->behavior_shortcut_count; ++i)
        if (!strcmp(config->behavior_shortcuts[i].id, id))
            return &config->behavior_shortcuts[i];
    if (config->behavior_shortcut_count >= BONGO_CAT_NEO_BEHAVIOR_CAP)
        return NULL;
    BongoCatNeoBehaviorShortcut *value =
        &config->behavior_shortcuts[config->behavior_shortcut_count++];
    memset(value, 0, sizeof(*value));
    snprintf(value->id, sizeof(value->id), "%s", id);
    return value;
}

static bool header(BongoCatNeoPreferences *value, struct nk_context *context,
    BongoCatNeoUIPalette p) {
    struct nk_rect bounds;
    nk_layout_row_dynamic(context, 42, 1);
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return false;
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    const char *title = tr(value,
        "pages.preference.model.behaviorModal.title", "Motions and expressions");
    text(context, canvas, nk_rect(bounds.x, bounds.y + 8,
        bounds.w - 42, 26), title, value->ui.label_font, p.text);
    struct nk_rect close = nk_rect(bounds.x + bounds.w - 34, bounds.y + 4, 30, 30);
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, close);
    if (hover) nk_fill_rect(canvas, close, 8, p.hover_pink);
    struct nk_color color = hover ? p.pink : p.muted;
    nk_stroke_line(canvas, close.x + 9, close.y + 9,
        close.x + 21, close.y + 21, 2, color);
    nk_stroke_line(canvas, close.x + 21, close.y + 9,
        close.x + 9, close.y + 21, 2, color);
    if (hover) bongo_cat_neo_ui_cursor_hover_rect(context, close,
        BONGO_CAT_NEO_UI_CURSOR_POINTER);
    return hit(context, close);
}

static bool segment_button(BongoCatNeoPreferences *value,
    struct nk_context *context, const char *label, bool active) {
    BongoCatNeoUIPalette p = bongo_cat_neo_ui_palette(bongo_cat_neo_ui_dark(context));
    struct nk_style_button style = context->style.button;
    style.normal = nk_style_item_color(active ? p.accent : p.field);
    style.hover = nk_style_item_color(active ? p.accent_hover : p.hover);
    style.active = nk_style_item_color(p.accent_pressed);
    style.border = 0; style.rounding = 8;
    style.text_normal = active ? nk_rgb(255, 255, 255) : p.muted;
    style.text_hover = active ? nk_rgb(255, 255, 255) : p.accent;
    style.text_active = nk_rgb(255, 255, 255);
    bongo_cat_neo_ui_cursor_hover_widget(context, BONGO_CAT_NEO_UI_CURSOR_POINTER);
    (void)value;
    return nk_button_label_styled(context, &style, label) != 0;
}

static void segments(BongoCatNeoPreferences *value, struct nk_context *context) {
    const char *motion = tr(value,
        "pages.preference.model.behaviorModal.labels.motion", "Motions");
    const char *expression = tr(value,
        "pages.preference.model.behaviorModal.labels.expression", "Expressions");
    nk_layout_row_dynamic(context, 38, 2);
    if (segment_button(value, context, motion, value->behavior_tab == 0))
        value->behavior_tab = 0;
    if (segment_button(value, context, expression, value->behavior_tab == 1))
        value->behavior_tab = 1;
}

static void keyboard_icon(struct nk_command_buffer *canvas, struct nk_rect r,
    struct nk_color color) {
    nk_stroke_rect(canvas, r, 3, 1.5f, color);
    for (int i = 0; i < 3; ++i)
        nk_stroke_line(canvas, r.x + 5 + i * 6, r.y + 6,
            r.x + 8 + i * 6, r.y + 6, 1, color);
    nk_stroke_line(canvas, r.x + 6, r.y + 13,
        r.x + r.w - 6, r.y + 13, 1, color);
}

static bool editor(BongoCatNeoPreferences *value, struct nk_context *context,
    struct nk_rect bounds, const char *id, const char *shortcut,
    BongoCatNeoUIPalette p) {
    bool active = bongo_cat_neo_preferences_shortcut_active(value, id);
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, bounds);
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    nk_fill_rect(canvas, bounds, 10, active ? p.hover_pink : p.field);
    nk_stroke_rect(canvas, bounds, 10, active ? 2.0f : 1.0f,
        active ? p.pink : (hover ? p.accent : p.border));
    keyboard_icon(canvas, nk_rect(bounds.x + 11, bounds.y + 9, 25, 18),
        active ? p.pink : p.muted);
    const char *shown = active ? tr(value,
        "components.shortcut.hints.pressRecordShortcut", "Press shortcut") :
        (shortcut[0] ? shortcut : tr(value,
        "components.shortcut.hints.clickRecordShortcut", "Click to record shortcut"));
    text(context, canvas, nk_rect(bounds.x + 45, bounds.y + 9,
        bounds.w - 54, 20), shown, value->ui.caption_font,
        active ? p.pink : p.muted);
    if (hover) bongo_cat_neo_ui_cursor_hover_rect(context, bounds,
        BONGO_CAT_NEO_UI_CURSOR_POINTER);
    return hit(context, bounds);
}

static bool matches_tab(const BongoCatNeoBehaviorEntry *entry, int tab) {
    return tab == 0 ? entry->kind == BONGO_CAT_NEO_BEHAVIOR_MOTION :
        entry->kind == BONGO_CAT_NEO_BEHAVIOR_EXPRESSION;
}

static void play_icon(struct nk_command_buffer *canvas, struct nk_rect r,
    struct nk_color color) {
    float x = r.x + r.w * .5f, y = r.y + r.h * .5f;
    nk_stroke_triangle(canvas, x - 4, y - 7, x - 4, y + 7,
        x + 7, y, 2, color);
}

static void behavior_row(BongoCatNeoPreferences *value,
    struct nk_context *context, BongoCatNeoBehaviorEntry *entry) {
    struct nk_rect bounds;
    nk_layout_row_dynamic(context, 58, 1);
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    BongoCatNeoUIPalette p = bongo_cat_neo_ui_palette(bongo_cat_neo_ui_dark(context));
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    text(context, canvas, nk_rect(bounds.x + 2, bounds.y + 18,
        NK_MAX(80.0f, bounds.w - 250), 24), entry->label,
        value->ui.caption_font, p.text);
    struct nk_rect play = nk_rect(bounds.x + bounds.w - 232, bounds.y + 10, 36, 36);
    struct nk_rect edit = nk_rect(play.x + 44, bounds.y + 10, 188, 36);
    bool play_hover = nk_input_is_mouse_hovering_rect(&context->input, play);
    nk_fill_rect(canvas, play, 9, play_hover ? p.hover_pink : p.field);
    nk_stroke_rect(canvas, play, 9, 1, play_hover ? p.pink : p.border);
    play_icon(canvas, play, play_hover ? p.pink : p.muted);
    if (play_hover) bongo_cat_neo_ui_cursor_hover_rect(context, play,
        BONGO_CAT_NEO_UI_CURSOR_POINTER);
    if (hit(context, play)) bongo_cat_neo_app_run_behavior(value->app, entry);
    BongoCatNeoBehaviorShortcut *shortcut = shortcut_for(&value->app->config,
        entry->id);
    if (!shortcut) return;
    char id[BONGO_CAT_NEO_ID_CAP + 16];
    snprintf(id, sizeof(id), "behavior-%.*s", (int)sizeof(id) - 10, entry->id);
    if (editor(value, context, edit, id, shortcut->shortcut, p))
        bongo_cat_neo_preferences_shortcut_begin(value, id,
            shortcut->shortcut, sizeof(shortcut->shortcut));
}

static void rows(BongoCatNeoPreferences *value, struct nk_context *context) {
    size_t shown = 0;
    for (size_t i = 0; i < value->app->behaviors.count; ++i) {
        BongoCatNeoBehaviorEntry *entry = &value->app->behaviors.entries[i];
        if (!matches_tab(entry, value->behavior_tab)) continue;
        behavior_row(value, context, entry); shown++;
    }
    if (!shown) {
        BongoCatNeoUIPalette p = bongo_cat_neo_ui_palette(
            bongo_cat_neo_ui_dark(context));
        nk_layout_row_dynamic(context, 70, 1);
        nk_label_colored(context, tr(value, "native.noBehaviors",
            "No items"), NK_TEXT_CENTERED, p.muted);
    }
}

void bongo_cat_neo_preferences_behavior_dialog_draw(
    BongoCatNeoPreferences *value, struct nk_context *context) {
    if (!bongo_cat_neo_preferences_behavior_dialog_active(value)) return;
    bongo_cat_neo_ui_cursor_reset(context);
    struct nk_rect region = nk_window_get_content_region(context);
    float width = NK_MIN(540.0f, region.w - 48.0f);
    float height = NK_MIN(500.0f, region.h - 48.0f);
    struct nk_rect bounds = nk_rect((region.w - width) * .5f,
        (region.h - height) * .5f, width, height);
    BongoCatNeoUIPalette p = bongo_cat_neo_ui_palette(bongo_cat_neo_ui_dark(context));
    nk_fill_rect(nk_window_get_canvas(context), region, 0,
        nk_rgba(0, 0, 0, bongo_cat_neo_ui_dark(context) ? 110 : 58));
    struct nk_style_window saved = context->style.window;
    context->style.window.fixed_background = nk_style_item_color(p.surface);
    context->style.window.background = p.surface;
    context->style.window.border_color = p.border;
    context->style.window.padding = nk_vec2(20, 16);
    context->style.window.spacing = nk_vec2(10, 10);
    context->style.window.border = 1; context->style.window.rounding = 18;
    if (nk_popup_begin(context, NK_POPUP_STATIC, "behavior-dialog",
        NK_WINDOW_NO_SCROLLBAR, bounds)) {
        bool close = header(value, context, p);
        segments(value, context);
        nk_layout_row_dynamic(context, height - 126, 1);
        if (nk_group_begin(context, "behavior-dialog-rows", 0)) {
            rows(value, context); nk_group_end(context);
        }
        if (close) {
            bongo_cat_neo_preferences_behavior_dialog_close(value);
            nk_popup_close(context);
        }
        nk_popup_end(context);
    } else bongo_cat_neo_preferences_behavior_dialog_close(value);
    context->style.window = saved;
}
