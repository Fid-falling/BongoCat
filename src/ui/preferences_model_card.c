#include "preferences_model_card.h"
#include "preferences_model_cover.h"
#include "preferences_notice.h"
#include "preferences_state.h"
#include "ui_animation.h"
#include "ui_backend.h"
#include "ui_icons.h"
#include "ui_paint.h"
#include "bongo_cat/i18n.h"
#include "bongo_cat/path.h"
#include "bongo_cat/platform.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *tr(BongoCatApp *app, const char *key,
    const char *fallback) {
    return bongo_cat_i18n_get(app->i18n, key, fallback);
}

static void text(struct nk_context *context, struct nk_command_buffer *canvas,
    struct nk_rect bounds, const char *value, struct nk_color color,
    const struct nk_user_font *font) {
    if (!font) font = context->style.font;
    nk_draw_text(canvas, bounds, value, nk_strlen(value), font,
        nk_rgba(0, 0, 0, 0), color);
}

static struct nk_rect outline_bounds(struct nk_rect bounds, float thickness) {
    float inset = thickness * .5f;
    return nk_rect(bounds.x + inset, bounds.y + inset,
        NK_MAX(1.0f, bounds.w - thickness),
        NK_MAX(1.0f, bounds.h - thickness));
}

bool bongo_cat_preferences_model_import_card(BongoCatPreferences *value,
    struct nk_context *context) {
    struct nk_rect bounds;
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return false;
    bounds.y += 5.0f; bounds.h += 1.0f;
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, bounds);
    float lift = bongo_cat_ui_animate_eased(context, "model-import-hover",
        hover ? 1.0f : 0.0f, 250.0f, BONGO_CAT_UI_EASE_STANDARD);
    bounds.y -= 2.0f * lift;
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    if (hover && p.effects) bongo_cat_ui_paint_shadow(context, bounds, 14,
        0, 8, 24, 0, nk_rgba(p.pink.r, p.pink.g, p.pink.b, 89));
    nk_fill_rect(canvas, bounds, 14, hover ? p.hover_pink : p.hover);
    bongo_cat_ui_paint_dashed_rounded(context, bounds, 14, 2, 5, 5,
        hover ? p.pink : p.accent);
    float cx = bounds.x + bounds.w * .5f, cy = bounds.y + 91;
    bongo_cat_preferences_icon_draw(value, canvas,
        BONGO_CAT_UI_ICON_UPLOAD, nk_rect(cx - 20, cy - 20, 40, 40), p.pink);
    const char *label = tr(value->app,
        "pages.preference.model.hints.clickOrDragToImport", "Import model directory");
    float width = value->ui.caption_font->width(value->ui.caption_font->userdata,
        value->ui.caption_font->height, label, nk_strlen(label));
    text(context, canvas, nk_rect(cx - width * .5f, cy + 30,
        NK_MIN(width + 1, bounds.w - 20), 24), label, p.accent,
        value->ui.caption_font);
    if (hover) bongo_cat_ui_cursor_hover_rect(context, bounds,
        BONGO_CAT_UI_CURSOR_POINTER);
    return hover && nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, bounds);
}

static void action_icon(BongoCatPreferences *value,
    struct nk_command_buffer *canvas, struct nk_rect bounds, int icon,
    struct nk_color color) {
    bongo_cat_preferences_icon_draw(value, canvas, icon,
        nk_rect(bounds.x + (bounds.w - 18) * .5f,
            bounds.y + (bounds.h - 18) * .5f, 18, 18), color);
}

void bongo_cat_preferences_model_select(BongoCatPreferences *value,
    const BongoCatModelEntry *entry) {
    BongoCatError error = {0};
    if (bongo_cat_app_select_model_with_error(value->app, entry->id, &error)) {
        bongo_cat_preferences_model_cover_capture(value->app, entry);
        bongo_cat_preferences_invalidate(value);
        return;
    }
    bongo_cat_preferences_notice_show(value->app, tr(value->app,
        "native.modelLoadFailed", "Unable to display this model"), true);
    bongo_cat_preferences_invalidate(value);
}

static void open_model_directory(BongoCatPreferences *value,
    const BongoCatModelEntry *entry) {
    const char *directory = entry->storage_directory[0] ?
        entry->storage_directory : entry->directory;
    if (bongo_cat_path_is_dir(directory) &&
        bongo_cat_platform_open_directory(directory)) return;
    bongo_cat_preferences_notice_show(value->app, tr(value->app,
        "pages.preference.model.hints.openDirectoryFailed",
        "Unable to open model directory"), true);
}

static void draw_cover(BongoCatPreferences *value,
    struct nk_command_buffer *canvas, struct nk_rect preview,
    const BongoCatModelEntry *entry, BongoCatUIPalette p) {
    float raster_scale = value->ui.raster_scale > 0.0f ?
        value->ui.raster_scale : 1.0f;
    const BongoCatModelCover *cover = bongo_cat_preferences_model_cover(
        value->app, entry, NK_MAX(1, (int)lroundf(preview.w * raster_scale)),
        NK_MAX(1, (int)lroundf(preview.h * raster_scale)));
    if (!cover) {
        bongo_cat_preferences_icon_draw(value, canvas, BONGO_CAT_UI_ICON_CAT,
            nk_rect(preview.x + (preview.w - 34) * .5f,
                preview.y + (preview.h - 34) * .5f, 34, 34), p.muted);
        return;
    }
    float scale = NK_MIN(preview.w / cover->width, preview.h / cover->height);
    struct nk_rect image = nk_rect(
        preview.x + (preview.w - cover->width * scale) * .5f,
        preview.y + (preview.h - cover->height * scale) * .5f,
        cover->width * scale, cover->height * scale);
    struct nk_image texture = nk_image_id((int)cover->texture);
    nk_draw_image(canvas, image, &texture, nk_rgb(255, 255, 255));
}

static void draw_actions(BongoCatPreferences *value,
    struct nk_context *context, struct nk_command_buffer *canvas,
    struct nk_rect bounds, const BongoCatModelEntry *entry,
    BongoCatUIPalette p, bool selected, bool *action_hover) {
    struct nk_rect actions = nk_rect(bounds.x + 1, bounds.y + bounds.h - 39,
        bounds.w - 2, 38);
    nk_fill_rect(canvas, actions, 0, p.field);
    bool deletable = !entry->preset && !entry->managed;
    float width = actions.w / (deletable ? 3.0f : 2.0f);
    struct nk_rect items[3] = {
        nk_rect(actions.x, actions.y, width, actions.h),
        nk_rect(actions.x + width, actions.y, width, actions.h),
        nk_rect(actions.x + width * 2, actions.y, width, actions.h)};
    bool hover[3] = {
        nk_input_is_mouse_hovering_rect(&context->input, items[0]),
        nk_input_is_mouse_hovering_rect(&context->input, items[1]),
        deletable && nk_input_is_mouse_hovering_rect(&context->input, items[2])};
    for (int i = 1; i < (deletable ? 3 : 2); ++i)
        nk_stroke_line(canvas, items[i].x, items[i].y + 10,
            items[i].x, items[i].y + items[i].h - 10, 1, p.border_subtle);
    for (int i = 0; i < 3; ++i)
        if (hover[i]) nk_fill_rect(canvas, items[i], 0, p.hover_pink);
    action_icon(value, canvas, items[0], BONGO_CAT_UI_ICON_SMILE,
        hover[0] ? p.pink : p.muted);
    action_icon(value, canvas, items[1], BONGO_CAT_UI_ICON_FOLDER,
        hover[1] ? p.pink : p.muted);
    if (deletable) action_icon(value, canvas, items[2], BONGO_CAT_UI_ICON_TRASH,
        hover[2] ? p.danger : p.muted);
    *action_hover = hover[0] || hover[1] || hover[2];
    if (hover[0] && nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, items[0])) {
        if (selected) bongo_cat_preferences_behavior_dialog_open(value);
        else bongo_cat_preferences_model_select(value, entry);
    } else if (hover[1] && nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, items[1])) open_model_directory(value, entry);
    else if (hover[2] && nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, items[2]))
        bongo_cat_preferences_remove_dialog_open(value->app, entry->id);
}

void bongo_cat_preferences_model_card(BongoCatPreferences *value,
    struct nk_context *context, const BongoCatModelEntry *entry) {
    BongoCatApp *app = value->app;
    bool selected = !strcmp(entry->id, app->config.current_model);
    struct nk_rect bounds;
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    bounds.y += 5.0f; bounds.h += 1.0f;
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, bounds);
    char animation_id[BONGO_CAT_ID_CAP + 24];
    snprintf(animation_id, sizeof(animation_id), "model-hover-%s", entry->id);
    float lift = bongo_cat_ui_animate_eased(context, animation_id,
        hover ? 1.0f : 0.0f, 250.0f, BONGO_CAT_UI_EASE_SWIFT);
    bounds.y -= 3.0f * lift;
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    if (selected && p.effects) bongo_cat_ui_paint_shadow(context, bounds,
        14, 0, 8, 20, 0, nk_rgba(p.pink.r, p.pink.g, p.pink.b, 89));
    else if (hover && p.effects) bongo_cat_ui_paint_shadow(context, bounds,
        14, 0, 10, 28, 0, nk_rgba(p.accent.r, p.accent.g, p.accent.b, 46));
    nk_fill_rect(canvas, bounds, 14, p.surface);
    float preview_height = NK_MIN(128.0f, bounds.w * 354.0f / 612.0f);
    struct nk_rect preview = nk_rect(bounds.x + 1, bounds.y + 1,
        bounds.w - 2, preview_height);
    nk_fill_rect(canvas, preview, 12, p.field);
    draw_cover(value, canvas, preview, entry, p);
    float info_y = preview.y + preview.h + 8;
    struct nk_rect name_bounds = nk_rect(bounds.x + 9, info_y + 1,
        bounds.w - 18, 30);
    bool name_hover = bongo_cat_preferences_model_name_draw(value,
        context, canvas, entry, name_bounds, p);
    bool action_hover = false;
    draw_actions(value, context, canvas, bounds, entry, p, selected,
        &action_hover);
    float outline_width = selected ? 2.0f : 1.0f;
    struct nk_rect outline = outline_bounds(bounds, outline_width);
    struct nk_color outline_color = selected ? p.pink :
        bongo_cat_ui_color_mix(p.border_subtle, p.accent, lift);
    nk_stroke_rect(canvas, outline, 14.0f - outline_width * .5f,
        outline_width, outline_color);
    if (!name_hover && (action_hover || hover))
        bongo_cat_ui_cursor_hover_rect(context, bounds,
            BONGO_CAT_UI_CURSOR_POINTER);
    if (!name_hover && !action_hover && hover &&
        nk_input_is_mouse_click_in_rect(&context->input,
            NK_BUTTON_LEFT, bounds))
        bongo_cat_preferences_model_select(value, entry);
}
