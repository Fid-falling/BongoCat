#include "preferences_state.h"
#include "preferences_widgets.h"
#include "preferences_notice.h"
#include "ui_backend.h"
#include "ui_catime.h"
#include "bongo_cat_neo/i18n.h"
#include "bongo_cat_neo/image.h"
#include "bongo_cat_neo/path.h"
#include "bongo_cat_neo/preferences.h"

#include <SDL3/SDL.h>
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
        if ((!app || slot->app == app) && slot->texture) glDeleteTextures(1, &slot->texture);
        if (!app || slot->app == app) memset(slot, 0, sizeof(*slot));
    }
}

static ModelCoverSlot *model_cover(BongoCatNeoApp *app, const BongoCatNeoModelEntry *entry) {
    char path[BONGO_CAT_NEO_PATH_CAP];
    if (!bongo_cat_neo_path_join(path, sizeof(path), entry->adapter_directory,
        "resources/cover.png") ||
        !bongo_cat_neo_path_is_file(path)) return NULL;
    ModelCoverSlot *empty = NULL;
    for (size_t i = 0; i < BONGO_CAT_NEO_MODEL_CAP; ++i) {
        ModelCoverSlot *slot = &cover_cache[i];
        if (slot->texture && slot->app == app && strcmp(slot->path, path) == 0) {
            slot->generation = cover_generation;
            return slot;
        }
        if (!slot->texture && !empty) empty = slot;
    }
    if (!empty) return NULL;
    BongoCatNeoError ignored = {0};
    empty->texture = bongo_cat_neo_image_texture_thumbnail(path, 320, 192,
        &empty->width, &empty->height, &ignored);
    if (!empty->texture) return NULL;
    empty->app = app;
    empty->generation = cover_generation;
    snprintf(empty->path, sizeof(empty->path), "%s", path);
    return empty;
}

static void prune_model_covers(BongoCatNeoApp *app) {
    for (size_t i = 0; i < BONGO_CAT_NEO_MODEL_CAP; ++i) {
        ModelCoverSlot *slot = &cover_cache[i];
        if (slot->app != app || !slot->texture || slot->generation == cover_generation) continue;
        glDeleteTextures(1, &slot->texture);
        memset(slot, 0, sizeof(*slot));
    }
}

static const char *tr(BongoCatNeoApp *app, const char *key, const char *fallback) {
    return bongo_cat_neo_i18n_get(app->i18n, key, fallback);
}

void bongo_cat_neo_preferences_import_path(BongoCatNeoApp *app, SDL_Window *window,
    const char *path) {
    (void)window;
    if (!app || !path || !path[0]) return;
    BongoCatNeoError error = {0};
    BongoCatNeoResult result = bongo_cat_neo_app_import_model(app, path, &error);
    const char *message = result == BONGO_CAT_NEO_OK
        ? tr(app, "pages.preference.model.hints.importSuccess", "Model imported")
        : error.message;
    if (app->smoke) {
        if (result != BONGO_CAT_NEO_OK) app->exit_code = 1;
    } else bongo_cat_neo_preferences_notice_show(app, message, result != BONGO_CAT_NEO_OK);
    bongo_cat_neo_preferences_invalidate(app->preferences);
    bongo_cat_neo_preferences_render(app->preferences);
}

static void draw_text(struct nk_context *context, struct nk_command_buffer *canvas,
    struct nk_rect bounds, const char *text, struct nk_color color) {
    nk_draw_text(canvas, bounds, text, (int)strlen(text), context->style.font,
        nk_rgba(0, 0, 0, 0), color);
}

static const char *mode_label(BongoCatNeoApp *app, BongoCatNeoModelMode mode) {
    if (mode == BONGO_CAT_NEO_MODE_KEYBOARD)
        return tr(app, "native.modeKeyboard", "Keyboard");
    if (mode == BONGO_CAT_NEO_MODE_GAMEPAD)
        return tr(app, "native.modeGamepad", "Gamepad");
    return tr(app, "native.modeStandard", "Standard");
}

static void draw_model_icon(struct nk_command_buffer *canvas, BongoCatNeoModelMode mode,
    struct nk_rect bounds, struct nk_color color) {
    float cx = bounds.x + bounds.w * .5f, cy = bounds.y + bounds.h * .5f;
    if (mode == BONGO_CAT_NEO_MODE_KEYBOARD) {
        struct nk_rect keyboard = nk_rect(cx - 38, cy - 20, 76, 40);
        nk_stroke_rect(canvas, keyboard, 7, 2, color);
        for (int row = 0; row < 2; ++row)
            for (int col = 0; col < 6; ++col)
                nk_fill_rect(canvas, nk_rect(keyboard.x + 9 + col * 10,
                    keyboard.y + 9 + row * 13, 6, 5), 1, color);
        return;
    }
    if (mode == BONGO_CAT_NEO_MODE_GAMEPAD) {
        nk_stroke_rect(canvas, nk_rect(cx - 42, cy - 23, 84, 46), 18, 2, color);
        nk_stroke_line(canvas, cx - 27, cy - 8, cx - 27, cy + 10, 3, color);
        nk_stroke_line(canvas, cx - 36, cy + 1, cx - 18, cy + 1, 3, color);
        nk_fill_circle(canvas, nk_rect(cx + 17, cy - 10, 8, 8), color);
        nk_fill_circle(canvas, nk_rect(cx + 28, cy + 2, 8, 8), color);
        return;
    }
    nk_stroke_circle(canvas, nk_rect(cx - 31, cy - 29, 62, 58), 2, color);
    nk_stroke_triangle(canvas, cx - 29, cy - 17, cx - 25, cy - 42,
        cx - 8, cy - 27, 2, color);
    nk_stroke_triangle(canvas, cx + 8, cy - 27, cx + 25, cy - 42,
        cx + 29, cy - 17, 2, color);
    nk_fill_circle(canvas, nk_rect(cx - 16, cy - 5, 5, 5), color);
    nk_fill_circle(canvas, nk_rect(cx + 11, cy - 5, 5, 5), color);
}

static BongoCatNeoBehaviorShortcut *behavior_shortcut(BongoCatNeoConfig *config,
    const char *id) {
    for (size_t i = 0; i < config->behavior_shortcut_count; ++i)
        if (strcmp(config->behavior_shortcuts[i].id, id) == 0)
            return &config->behavior_shortcuts[i];
    if (config->behavior_shortcut_count >= BONGO_CAT_NEO_BEHAVIOR_CAP) return NULL;
    BongoCatNeoBehaviorShortcut *value =
        &config->behavior_shortcuts[config->behavior_shortcut_count++];
    snprintf(value->id, sizeof(value->id), "%s", id);
    return value;
}

static bool import_card(BongoCatNeoApp *app, struct nk_context *context) {
    struct nk_rect bounds;
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return false;
    BongoCatNeoUIPalette palette = bongo_cat_neo_ui_palette(bongo_cat_neo_ui_dark(context));
    bool interactive = !bongo_cat_neo_preferences_remove_dialog_active(app);
    bool hover = interactive &&
        nk_input_is_mouse_hovering_rect(&context->input, bounds);
    if (hover) bongo_cat_neo_ui_cursor_hover_rect(context, bounds,
        BONGO_CAT_NEO_UI_CURSOR_POINTER);
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    nk_fill_rect(canvas, bounds, 10, hover ? palette.hover : palette.surface);
    nk_stroke_rect(canvas, bounds, 10, 2, palette.accent);
    float cx = bounds.x + bounds.w * .5f;
    nk_stroke_circle(canvas, nk_rect(cx - 20, bounds.y + 35, 40, 40),
        2, palette.accent);
    nk_stroke_line(canvas, cx - 9, bounds.y + 55, cx + 9,
        bounds.y + 55, 3, palette.accent);
    nk_stroke_line(canvas, cx, bounds.y + 46, cx,
        bounds.y + 64, 3, palette.accent);
    const char *label = tr(app, "pages.preference.model.hints.clickOrDragToImport",
        "Import model directory");
    draw_text(context, canvas, nk_rect(bounds.x + 12, bounds.y + 100,
        bounds.w - 24, 28), label, palette.accent);
    return interactive && nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, bounds);
}

static bool model_card(BongoCatNeoApp *app, struct nk_context *context,
    BongoCatNeoModelEntry *entry) {
    bool selected = strcmp(entry->id, app->config.current_model) == 0;
    struct nk_rect bounds;
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return false;
    BongoCatNeoUIPalette palette = bongo_cat_neo_ui_palette(bongo_cat_neo_ui_dark(context));
    bool interactive = !bongo_cat_neo_preferences_remove_dialog_active(app);
    bool hover = interactive &&
        nk_input_is_mouse_hovering_rect(&context->input, bounds);
    if (hover) bongo_cat_neo_ui_cursor_hover_rect(context, bounds,
        BONGO_CAT_NEO_UI_CURSOR_POINTER);
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    struct nk_color border = selected ? palette.accent : palette.border;
    nk_fill_rect(canvas, bounds, 10, hover ? palette.hover : palette.surface);
    nk_stroke_rect(canvas, bounds, 10, selected ? 2.0f : 1.0f, border);
    struct nk_rect preview = nk_rect(bounds.x + 8, bounds.y + 8, bounds.w - 16, 92);
    nk_fill_rect(canvas, preview, 8,
        selected ? palette.selection : palette.field);
    ModelCoverSlot *cover = model_cover(app, entry);
    if (cover) {
        float scale = NK_MIN(preview.w / cover->width, preview.h / cover->height);
        struct nk_rect image_bounds = nk_rect(
            preview.x + (preview.w - cover->width * scale) * .5f,
            preview.y + (preview.h - cover->height * scale) * .5f,
            cover->width * scale, cover->height * scale);
        struct nk_image image = nk_image_id((int)cover->texture);
        nk_draw_image(canvas, image_bounds, &image, nk_rgb(255, 255, 255));
    } else draw_model_icon(canvas, entry->mode, preview,
        selected ? palette.accent : palette.muted);
    draw_text(context, canvas, nk_rect(bounds.x + 12, bounds.y + 108,
        bounds.w - 24, 24), entry->id, palette.text);
    draw_text(context, canvas, nk_rect(bounds.x + 12, bounds.y + 134,
        bounds.w - 24, 22), mode_label(app, entry->mode), palette.muted);
    struct nk_rect status = nk_rect(bounds.x + bounds.w - 94, bounds.y + 132, 82, 24);
    if (selected) {
        nk_fill_circle(canvas, nk_rect(status.x, status.y + 5, 13, 13),
            palette.accent);
        draw_text(context, canvas, nk_rect(status.x + 19, status.y,
            status.w - 19, status.h), tr(app, "native.selected", "Selected"),
            palette.accent);
    }
    struct nk_rect remove = nk_rect(bounds.x + bounds.w - 38, bounds.y + 8, 30, 30);
    bool remove_hover = interactive && !entry->preset &&
        nk_input_is_mouse_hovering_rect(&context->input, remove);
    if (!entry->preset) {
        if (remove_hover) nk_fill_rect(canvas, remove, 6,
            palette.danger_background);
        nk_stroke_line(canvas, remove.x + 9, remove.y + 9,
            remove.x + 21, remove.y + 21, 2, palette.danger);
        nk_stroke_line(canvas, remove.x + 21, remove.y + 9,
            remove.x + 9, remove.y + 21, 2, palette.danger);
    }
    if (remove_hover) bongo_cat_neo_ui_cursor_hover_rect(context, remove,
        BONGO_CAT_NEO_UI_CURSOR_POINTER);
    if (remove_hover && nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, remove)) {
        bongo_cat_neo_preferences_remove_dialog_open(app, entry->id);
        return false;
    }
    if (interactive && nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, bounds) &&
        !selected) bongo_cat_neo_app_select_model(app, entry->id);
    return false;
}

static void behavior_rows(BongoCatNeoPreferences *value, struct nk_context *context) {
    BongoCatNeoApp *app = value->app;
    if (!app->config.model.behavior || !app->behaviors.count) return;
    bongo_cat_neo_pref_section(context, tr(app, "pages.preference.model.behaviorModal.title",
        "Motions and expressions"));
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        BongoCatNeoBehaviorEntry *entry = &app->behaviors.entries[i];
        BongoCatNeoBehaviorShortcut *shortcut = behavior_shortcut(&app->config, entry->id);
        if (!shortcut) break;
        char id[BONGO_CAT_NEO_ID_CAP + 16];
        snprintf(id, sizeof(id), "behavior-%s", entry->id);
        bool active = bongo_cat_neo_preferences_shortcut_active(value, id);
        bool clicked = bongo_cat_neo_pref_edit(context, id, entry->label, "",
            shortcut->shortcut, active, tr(app,
            "components.shortcut.hints.clickRecordShortcut", "Click to record shortcut"),
            tr(app, "components.shortcut.hints.pressRecordShortcut", "Press shortcut"));
        if (clicked) bongo_cat_neo_preferences_shortcut_begin(value, id,
            shortcut->shortcut, sizeof(shortcut->shortcut));
    }
}

void bongo_cat_neo_preferences_page_model(BongoCatNeoPreferences *value,
    struct nk_context *context) {
    BongoCatNeoApp *app = value->app;
    cover_generation++;
    if (!cover_generation) cover_generation++;
    bongo_cat_neo_pref_section(context, tr(app, "pages.preference.model.title", "Installed models"));
    float width = nk_window_get_content_region(context).w;
    int columns = width >= 690 ? 3 : width >= 450 ? 2 : 1;
    nk_layout_row_dynamic(context, 168, columns);
    if (import_card(app, context))
        bongo_cat_neo_preferences_request_model_import(app->preferences);
    for (size_t i = 0; i < app->models.count; ++i)
        if (model_card(app, context, &app->models.entries[i])) break;
    prune_model_covers(app);
    behavior_rows(value, context);
}
