#include "preferences_state.h"
#include "preferences_model_cover.h"
#include "preferences_widgets.h"
#include "preferences_notice.h"
#include "ui_backend.h"
#include "ui_catime.h"
#include "ui_animation.h"
#include "ui_paint.h"
#include "ui_icons.h"
#include "bongo_cat_neo/i18n.h"
#include "bongo_cat_neo/preferences.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *tr(BongoCatNeoApp *app, const char *key,
    const char *fallback) {
    return bongo_cat_neo_i18n_get(app->i18n, key, fallback);
}

void bongo_cat_neo_preferences_import_path(BongoCatNeoApp *app,
    SDL_Window *window, const char *path) {
    (void)window;
    if (!app || !path || !path[0]) return;
    BongoCatNeoError error = {0};
    BongoCatNeoResult result = bongo_cat_neo_app_import_model(app, path, &error);
    const char *message = result == BONGO_CAT_NEO_OK ? tr(app,
        "pages.preference.model.hints.importSuccess", "Model imported") :
        error.message;
    if (app->smoke) {
        if (result != BONGO_CAT_NEO_OK) app->exit_code = 1;
    } else bongo_cat_neo_preferences_notice_show(app, message,
        result != BONGO_CAT_NEO_OK);
    bongo_cat_neo_preferences_invalidate(app->preferences);
    bongo_cat_neo_preferences_render(app->preferences);
}

static const char *mode_label(BongoCatNeoApp *app, BongoCatNeoModelMode mode) {
    if (mode == BONGO_CAT_NEO_MODE_KEYBOARD)
        return tr(app, "native.modeKeyboard", "Keyboard");
    if (mode == BONGO_CAT_NEO_MODE_GAMEPAD)
        return tr(app, "native.modeGamepad", "Gamepad");
    return tr(app, "native.modeStandard", "Standard");
}

static const char *model_name(const BongoCatNeoModelEntry *entry) {
    if (!strcmp(entry->id, "standard")) return "Standard";
    if (!strcmp(entry->id, "keyboard")) return "Keyboard";
    if (!strcmp(entry->id, "gamepad")) return "Gamepad";
    return entry->id;
}

static void text(struct nk_context *context, struct nk_command_buffer *canvas,
    struct nk_rect bounds, const char *value, struct nk_color color,
    const struct nk_user_font *font) {
    if (!font) font = context->style.font;
    nk_draw_text(canvas, bounds, value, nk_strlen(value), font,
        nk_rgba(0, 0, 0, 0), color);
}

static void dashed_rect(struct nk_command_buffer *canvas, struct nk_rect r,
    struct nk_color color) {
    const float dash = 5.0f, gap = 5.0f;
    for (float x = r.x + 14; x < r.x + r.w - 10; x += dash + gap) {
        float end = NK_MIN(x + dash, r.x + r.w - 10);
        nk_stroke_line(canvas, x, r.y, end, r.y, 2, color);
        nk_stroke_line(canvas, x, r.y + r.h, end, r.y + r.h, 2, color);
    }
    for (float y = r.y + 14; y < r.y + r.h - 10; y += dash + gap) {
        float end = NK_MIN(y + dash, r.y + r.h - 10);
        nk_stroke_line(canvas, r.x, y, r.x, end, 2, color);
        nk_stroke_line(canvas, r.x + r.w, y, r.x + r.w, end, 2, color);
    }
}

static bool import_card(BongoCatNeoPreferences *value,
    struct nk_context *context) {
    struct nk_rect bounds;
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return false;
    bounds.x += 2.0f; bounds.w += 2.0f;
    bounds.y += 5.0f; bounds.h += 1.0f;
    BongoCatNeoUIPalette p = bongo_cat_neo_ui_palette(bongo_cat_neo_ui_dark(context));
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, bounds);
    float lift = bongo_cat_neo_ui_animate_eased(context, "model-import-hover",
        hover ? 1.0f : 0.0f, 250.0f, BONGO_CAT_NEO_UI_EASE_STANDARD);
    bounds.y -= 2.0f * lift;
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    if (hover && p.effects) bongo_cat_neo_ui_paint_shadow(context, bounds, 14,
        0, 8, 24, 0, nk_rgba(p.pink.r, p.pink.g, p.pink.b, 89));
    nk_fill_rect(canvas, bounds, 14, hover ? p.hover_pink : p.hover);
    dashed_rect(canvas, bounds, hover ? p.pink : p.accent);
    float cx = bounds.x + bounds.w * .5f, cy = bounds.y + 94;
    bongo_cat_neo_preferences_icon_draw(value, canvas,
        BONGO_CAT_NEO_UI_ICON_UPLOAD, nk_rect(cx - 20, cy - 20, 40, 40), p.pink);
    const char *label = tr(value->app,
        "pages.preference.model.hints.clickOrDragToImport", "Import model directory");
    float width = value->ui.caption_font->width(value->ui.caption_font->userdata,
        value->ui.caption_font->height, label, nk_strlen(label));
    text(context, canvas, nk_rect(cx - width * .5f, cy + 30,
        NK_MIN(width + 1, bounds.w - 20), 24), label, p.accent,
        value->ui.caption_font);
    if (hover) bongo_cat_neo_ui_cursor_hover_rect(context, bounds,
        BONGO_CAT_NEO_UI_CURSOR_POINTER);
    return hover && nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, bounds);
}

static void action_icon(BongoCatNeoPreferences *value,
    struct nk_command_buffer *canvas, struct nk_rect r, int icon,
    struct nk_color color) {
    bongo_cat_neo_preferences_icon_draw(value, canvas, icon,
        nk_rect(r.x + (r.w - 18) * .5f, r.y + (r.h - 18) * .5f, 18, 18), color);
}

static void model_card(BongoCatNeoPreferences *value, struct nk_context *context,
    BongoCatNeoModelEntry *entry) {
    BongoCatNeoApp *app = value->app;
    bool selected = !strcmp(entry->id, app->config.current_model);
    struct nk_rect bounds;
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    bounds.x += 2.0f; bounds.w += 2.0f;
    bounds.y += 5.0f; bounds.h += 1.0f;
    BongoCatNeoUIPalette p = bongo_cat_neo_ui_palette(bongo_cat_neo_ui_dark(context));
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, bounds);
    char animation_id[BONGO_CAT_NEO_ID_CAP + 24];
    snprintf(animation_id, sizeof(animation_id), "model-hover-%s", entry->id);
    float lift = bongo_cat_neo_ui_animate_eased(context, animation_id,
        hover ? 1.0f : 0.0f, 250.0f, BONGO_CAT_NEO_UI_EASE_SWIFT);
    bounds.y -= 3.0f * lift;
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    if (selected && p.effects) bongo_cat_neo_ui_paint_shadow(context, bounds,
        14, 0, 8, 20, 0, nk_rgba(p.pink.r, p.pink.g, p.pink.b, 89));
    else if (hover && p.effects) bongo_cat_neo_ui_paint_shadow(context, bounds,
        14, 0, 10, 28, 0, nk_rgba(p.accent.r, p.accent.g, p.accent.b, 46));
    nk_fill_rect(canvas, bounds, 14, p.surface);
    nk_stroke_rect(canvas, bounds, 14, selected ? 2.0f : 1.0f,
        selected ? p.pink : (hover ? p.accent : p.border_subtle));
    float preview_height = NK_MIN(128.0f, bounds.w * 354.0f / 612.0f);
    struct nk_rect preview = nk_rect(bounds.x + 1, bounds.y + 1,
        bounds.w - 2, preview_height);
    nk_fill_rect(canvas, preview, 12, p.field);
    int logical_width = 0, logical_height = 0, pixel_width = 0, pixel_height = 0;
    SDL_GetWindowSize(value->window, &logical_width, &logical_height);
    SDL_GetWindowSizeInPixels(value->window, &pixel_width, &pixel_height);
    float scale_x = logical_width > 0 ? (float)pixel_width / logical_width : 1;
    float scale_y = logical_height > 0 ? (float)pixel_height / logical_height : 1;
    const BongoCatNeoModelCover *cover = bongo_cat_neo_preferences_model_cover(
        app, entry, NK_MAX(1, (int)lroundf(preview.w * scale_x)),
        NK_MAX(1, (int)lroundf(preview.h * scale_y)));
    if (cover) {
        float scale = NK_MIN(preview.w / cover->width, preview.h / cover->height);
        struct nk_rect image = nk_rect(preview.x + (preview.w - cover->width * scale) * .5f,
            preview.y + (preview.h - cover->height * scale) * .5f,
            cover->width * scale, cover->height * scale);
        struct nk_image texture = nk_image_id((int)cover->texture);
        nk_draw_image(canvas, image, &texture, nk_rgb(255, 255, 255));
    }
    float info_y = preview.y + preview.h + 8;
    text(context, canvas, nk_rect(bounds.x + 13, info_y, bounds.w - 26, 22),
        model_name(entry), p.text, value->ui.label_font);
    text(context, canvas, nk_rect(bounds.x + 13, info_y + 24, bounds.w - 26, 20),
        mode_label(app, entry->mode), p.muted, value->ui.caption_font);
    struct nk_rect actions = nk_rect(bounds.x + 1, bounds.y + bounds.h - 39,
        bounds.w - 2, 38);
    nk_fill_rect(canvas, actions, 0, p.field);
    bool deletable = !entry->preset && !entry->managed;
    float action_width = actions.w / (deletable ? 3.0f : 2.0f);
    struct nk_rect first = nk_rect(actions.x, actions.y, action_width, actions.h);
    struct nk_rect second = nk_rect(first.x + first.w, actions.y,
        action_width, actions.h);
    struct nk_rect third = nk_rect(second.x + second.w, actions.y,
        action_width, actions.h);
    nk_stroke_line(canvas, second.x, second.y + 10, second.x,
        second.y + second.h - 10, 1, p.border_subtle);
    bool first_hover = nk_input_is_mouse_hovering_rect(&context->input, first);
    bool second_hover = nk_input_is_mouse_hovering_rect(&context->input, second);
    bool third_hover = deletable &&
        nk_input_is_mouse_hovering_rect(&context->input, third);
    if (first_hover) nk_fill_rect(canvas, first, 0, p.hover_pink);
    if (second_hover) nk_fill_rect(canvas, second, 0, p.hover_pink);
    if (third_hover) nk_fill_rect(canvas, third, 0, p.hover_pink);
    action_icon(value, canvas, first, selected ? BONGO_CAT_NEO_UI_ICON_SMILE :
        BONGO_CAT_NEO_UI_ICON_CHECK,
        first_hover ? p.pink : p.muted);
    action_icon(value, canvas, second, BONGO_CAT_NEO_UI_ICON_FOLDER,
        second_hover ? p.pink : p.muted);
    if (deletable) {
        nk_stroke_line(canvas, third.x, third.y + 10, third.x,
            third.y + third.h - 10, 1, p.border_subtle);
        action_icon(value, canvas, third, BONGO_CAT_NEO_UI_ICON_TRASH,
            third_hover ? p.danger : p.muted);
    }
    if (first_hover || second_hover || third_hover || hover)
        bongo_cat_neo_ui_cursor_hover_rect(context, bounds,
            BONGO_CAT_NEO_UI_CURSOR_POINTER);
    if (first_hover && nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, first)) {
        if (selected && app->config.model.behavior)
            bongo_cat_neo_preferences_behavior_dialog_open(value);
        else bongo_cat_neo_app_select_model(app, entry->id);
    } else if (second_hover && nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, second)) bongo_cat_neo_app_select_model(app, entry->id);
    else if (third_hover && nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, third))
        bongo_cat_neo_preferences_remove_dialog_open(app, entry->id);
    else if (hover && nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, bounds)) bongo_cat_neo_app_select_model(app, entry->id);
}

static bool is_builtin(const char *id) {
    return !strcmp(id, "standard") || !strcmp(id, "keyboard") ||
        !strcmp(id, "gamepad");
}

static void draw_named(BongoCatNeoPreferences *value, struct nk_context *context,
    const char *id) {
    for (size_t i = 0; i < value->app->models.count; ++i)
        if (!strcmp(value->app->models.entries[i].id, id)) {
            model_card(value, context, &value->app->models.entries[i]); return;
        }
}

void bongo_cat_neo_preferences_page_model(BongoCatNeoPreferences *value,
    struct nk_context *context) {
    BongoCatNeoApp *app = value->app;
    bongo_cat_neo_preferences_model_covers_begin();
    bongo_cat_neo_pref_section(context,
        tr(app, "pages.preference.model.title", "Installed models"));
    float width = nk_window_get_content_region(context).w;
    int columns = width >= 780 ? 4 : width >= 620 ? 3 : width >= 400 ? 2 : 1;
    struct nk_vec2 old_spacing = context->style.window.spacing;
    context->style.window.spacing = nk_vec2(14, 17);
    nk_layout_row_dynamic(context, 222, columns);
    if (import_card(value, context))
        bongo_cat_neo_preferences_request_model_import(app->preferences);
    draw_named(value, context, "standard");
    draw_named(value, context, "keyboard");
    draw_named(value, context, "gamepad");
    for (size_t i = 0; i < app->models.count; ++i)
        if (!is_builtin(app->models.entries[i].id))
            model_card(value, context, &app->models.entries[i]);
    context->style.window.spacing = old_spacing;
    bongo_cat_neo_preferences_model_covers_prune(app);
}
