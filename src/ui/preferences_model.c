#include "preferences_state.h"
#include "preferences_widgets.h"
#include "preferences_notice.h"
#include "ui_backend.h"
#include "ui_catime.h"
#include "ui_animation.h"
#include "bongo_cat_neo/i18n.h"
#include "bongo_cat_neo/image.h"
#include "bongo_cat_neo/path.h"
#include "bongo_cat_neo/preferences.h"

#include <SDL3/SDL_opengl.h>
#include <stdio.h>
#include <string.h>

typedef struct ModelCoverSlot {
    BongoCatNeoApp *app;
    char path[BONGO_CAT_NEO_PATH_CAP];
    GLuint texture;
    int width, height;
    uint64_t generation;
} ModelCoverSlot;

static ModelCoverSlot cover_cache[BONGO_CAT_NEO_MODEL_CAP];
static uint64_t cover_generation;

void bongo_cat_neo_preferences_model_cache_clear(BongoCatNeoApp *app) {
    bongo_cat_neo_preferences_remove_dialog_clear(app);
    for (size_t i = 0; i < BONGO_CAT_NEO_MODEL_CAP; ++i) {
        ModelCoverSlot *slot = &cover_cache[i];
        if ((!app || slot->app == app) && slot->texture)
            glDeleteTextures(1, &slot->texture);
        if (!app || slot->app == app) memset(slot, 0, sizeof(*slot));
    }
}

static ModelCoverSlot *model_cover(BongoCatNeoApp *app,
    const BongoCatNeoModelEntry *entry) {
    char path[BONGO_CAT_NEO_PATH_CAP];
    if (!bongo_cat_neo_path_join(path, sizeof(path), entry->adapter_directory,
        "resources/cover.png") || !bongo_cat_neo_path_is_file(path)) return NULL;
    ModelCoverSlot *empty = NULL;
    for (size_t i = 0; i < BONGO_CAT_NEO_MODEL_CAP; ++i) {
        ModelCoverSlot *slot = &cover_cache[i];
        if (slot->texture && slot->app == app && !strcmp(slot->path, path)) {
            slot->generation = cover_generation; return slot;
        }
        if (!slot->texture && !empty) empty = slot;
    }
    if (!empty) return NULL;
    BongoCatNeoError ignored = {0};
    empty->texture = bongo_cat_neo_image_texture_thumbnail(path, 384, 224,
        &empty->width, &empty->height, &ignored);
    if (!empty->texture) return NULL;
    empty->app = app; empty->generation = cover_generation;
    snprintf(empty->path, sizeof(empty->path), "%s", path);
    return empty;
}

static void prune_covers(BongoCatNeoApp *app) {
    for (size_t i = 0; i < BONGO_CAT_NEO_MODEL_CAP; ++i) {
        ModelCoverSlot *slot = &cover_cache[i];
        if (slot->app != app || !slot->texture ||
            slot->generation == cover_generation) continue;
        glDeleteTextures(1, &slot->texture); memset(slot, 0, sizeof(*slot));
    }
}

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
    const float dash = 7.0f, gap = 5.0f;
    for (float x = r.x + 8; x < r.x + r.w - 8; x += dash + gap) {
        float end = NK_MIN(x + dash, r.x + r.w - 8);
        nk_stroke_line(canvas, x, r.y, end, r.y, 2, color);
        nk_stroke_line(canvas, x, r.y + r.h, end, r.y + r.h, 2, color);
    }
    for (float y = r.y + 8; y < r.y + r.h - 8; y += dash + gap) {
        float end = NK_MIN(y + dash, r.y + r.h - 8);
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
    float lift = bongo_cat_neo_ui_animate(context, "model-import-hover",
        hover ? 1.0f : 0.0f, 250.0f);
    bounds.y -= 2.0f * lift;
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    nk_fill_rect(canvas, bounds, 13, hover ? p.hover_pink : p.selection);
    dashed_rect(canvas, bounds, hover ? p.pink : p.accent);
    float cx = bounds.x + bounds.w * .5f, cy = bounds.y + 94;
    nk_stroke_rect(canvas, nk_rect(cx - 16, cy - 17, 32, 34), 8, 2, p.pink);
    nk_stroke_line(canvas, cx, cy - 8, cx, cy + 8, 2, p.pink);
    nk_stroke_line(canvas, cx - 6, cy - 2, cx, cy - 8, 2, p.pink);
    nk_stroke_line(canvas, cx + 6, cy - 2, cx, cy - 8, 2, p.pink);
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

static void action_icon(struct nk_command_buffer *canvas, struct nk_rect r,
    bool behavior, bool folder, bool trash, struct nk_color color) {
    float x = r.x + r.w * .5f, y = r.y + r.h * .5f;
    if (trash) {
        nk_stroke_rect(canvas, nk_rect(x - 6, y - 5, 12, 12), 1, 1.5f, color);
        nk_stroke_line(canvas, x - 8, y - 8, x + 8, y - 8, 1.5f, color);
        nk_stroke_line(canvas, x - 3, y - 11, x + 3, y - 11, 1.5f, color);
    } else if (folder) {
        nk_stroke_rect(canvas, nk_rect(x - 8, y - 5, 17, 12), 2, 1.5f, color);
        nk_stroke_line(canvas, x - 7, y - 7, x - 1, y - 7, 1.5f, color);
    } else if (behavior) {
        nk_stroke_circle(canvas, nk_rect(x - 7, y - 7, 14, 14), 1.5f, color);
        nk_fill_circle(canvas, nk_rect(x - 4, y - 2, 2, 2), color);
        nk_fill_circle(canvas, nk_rect(x + 2, y - 2, 2, 2), color);
        nk_stroke_curve(canvas, x - 4, y + 2, x - 2, y + 5,
            x + 2, y + 5, x + 4, y + 2, 1.5f, color);
    } else {
        nk_stroke_circle(canvas, nk_rect(x - 7, y - 7, 14, 14), 1.5f, color);
        nk_stroke_line(canvas, x - 3, y, x, y + 3, 1.5f, color);
        nk_stroke_line(canvas, x, y + 3, x + 5, y - 3, 1.5f, color);
    }
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
    float lift = bongo_cat_neo_ui_animate(context, animation_id,
        hover ? 1.0f : 0.0f, 250.0f);
    bounds.y -= 3.0f * lift;
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    nk_fill_rect(canvas, bounds, 13, p.surface);
    nk_stroke_rect(canvas, bounds, 13, selected ? 2.0f : 1.0f,
        selected ? p.pink : (hover ? p.accent : p.border));
    float preview_height = NK_MIN(128.0f, bounds.w * 354.0f / 612.0f);
    struct nk_rect preview = nk_rect(bounds.x + 1, bounds.y + 1,
        bounds.w - 2, preview_height);
    nk_fill_rect(canvas, preview, 12, p.field);
    ModelCoverSlot *cover = model_cover(app, entry);
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
        second.y + second.h - 10, 1, p.border);
    bool first_hover = nk_input_is_mouse_hovering_rect(&context->input, first);
    bool second_hover = nk_input_is_mouse_hovering_rect(&context->input, second);
    bool third_hover = deletable &&
        nk_input_is_mouse_hovering_rect(&context->input, third);
    action_icon(canvas, first, selected, false, false,
        first_hover ? p.pink : p.muted);
    action_icon(canvas, second, false, true, false,
        second_hover ? p.pink : p.muted);
    if (deletable) {
        nk_stroke_line(canvas, third.x, third.y + 10, third.x,
            third.y + third.h - 10, 1, p.border);
        action_icon(canvas, third, false, false, true,
            third_hover ? p.danger : p.muted);
    }
    if (first_hover || second_hover || third_hover || hover)
        bongo_cat_neo_ui_cursor_hover_rect(context, bounds,
            BONGO_CAT_NEO_UI_CURSOR_POINTER);
    if (first_hover && nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, first)) {
        if (selected && app->config.model.behavior) value->behavior_dialog = true;
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
    cover_generation++; if (!cover_generation) cover_generation++;
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
    prune_covers(app);
}
