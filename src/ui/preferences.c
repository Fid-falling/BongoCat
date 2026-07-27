#include "bongo_cat_neo/preferences.h"
#include "bongo_cat_neo/app.h"
#include "bongo_cat_neo/i18n.h"
#include "bongo_cat_neo/memory.h"
#include "bongo_cat_neo/platform.h"
#include "preferences_state.h"
#include "ui_catime.h"
#include "ui_animation.h"
#include "ui_font.h"
#include "ui_native_theme.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <stdlib.h>

#define BONGO_CAT_NEO_PREF_WIDTH 900
#define BONGO_CAT_NEO_PREF_HEIGHT 680
#define BONGO_CAT_NEO_PREF_MIN_WIDTH 720
#define BONGO_CAT_NEO_PREF_MIN_HEIGHT 560

int bongo_cat_neo_preferences_resolved_theme(const BongoCatNeoPreferences *value) {
    if (value->app->config.app.theme == BONGO_CAT_NEO_THEME_DARK) return 1;
    if (value->app->config.app.theme == BONGO_CAT_NEO_THEME_LIGHT) return 0;
    return SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK;
}
void bongo_cat_neo_preferences_apply_theme(BongoCatNeoPreferences *value) {
    bool topmost = value->app->config.window.always_on_top;
    if (((SDL_GetWindowFlags(value->window) & SDL_WINDOW_ALWAYS_ON_TOP) != 0) != topmost)
        SDL_SetWindowAlwaysOnTop(value->window, topmost);
    int dark = bongo_cat_neo_preferences_resolved_theme(value);
    if (dark == value->style_theme) return;
    value->style_theme = dark;
    bongo_cat_neo_ui_apply_theme(&value->ui.context, dark != 0);
    bongo_cat_neo_ui_native_theme_apply(value->window, dark != 0);
}

static SDL_HitTestResult SDLCALL preference_hit_test(SDL_Window *window,
    const SDL_Point *point, void *data) {
    BongoCatNeoPreferences *value = data;
    if (!value || !point || bongo_cat_neo_preferences_remove_dialog_active(value->app))
        return SDL_HITTEST_NORMAL;
    int width = 0, height = 0;
    SDL_GetWindowSize(window, &width, &height);
    const int edge = 7;
    bool left = point->x < edge, right = point->x >= width - edge;
    bool top = point->y < edge, bottom = point->y >= height - edge;
    if (top && left) return SDL_HITTEST_RESIZE_TOPLEFT;
    if (top && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
    if (bottom && left) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    if (top) return SDL_HITTEST_RESIZE_TOP;
    if (right) return SDL_HITTEST_RESIZE_RIGHT;
    if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
    if (left) return SDL_HITTEST_RESIZE_LEFT;
    return bongo_cat_neo_ui_title_drag_hit((float)point->x, (float)point->y,
        (float)width) ? SDL_HITTEST_DRAGGABLE : SDL_HITTEST_NORMAL;
}

static void preferred_window_size(int *width, int *height) {
    *width = BONGO_CAT_NEO_PREF_WIDTH;
    *height = BONGO_CAT_NEO_PREF_HEIGHT;
    SDL_Rect usable;
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    if (!display || !SDL_GetDisplayUsableBounds(display, &usable)) return;
    int available_width = usable.w - 48;
    int available_height = usable.h - 48;
    if (available_width > 0 && *width > available_width) *width = available_width;
    if (available_height > 0 && *height > available_height) *height = available_height;
    if (*width < 640) *width = 640;
    if (*height < 520) *height = 520;
}

static bool open_window(BongoCatNeoPreferences *value) {
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS |
        SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
    int width, height;
    preferred_window_size(&width, &height);
#ifdef _WIN32
    SDL_SetHint(SDL_HINT_WINDOWS_ERASE_BACKGROUND_MODE, "0");
#endif
    value->window = SDL_CreateWindow(BONGO_CAT_NEO_NAME, width, height, flags);
    if (!value->window) return false;
    SDL_SetWindowMinimumSize(value->window, BONGO_CAT_NEO_PREF_MIN_WIDTH,
        BONGO_CAT_NEO_PREF_MIN_HEIGHT);
    SDL_SetWindowPosition(value->window, SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED);
    value->native_drag = SDL_SetWindowHitTest(value->window,
        preference_hit_test, value);
    value->gl_context = value->app->gl_context;
    if (!SDL_GL_MakeCurrent(value->window, value->gl_context)) {
        SDL_GL_MakeCurrent(value->app->window, value->app->gl_context);
        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
        value->gl_context = SDL_GL_CreateContext(value->window);
        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
        value->owns_gl_context = value->gl_context != NULL;
        if (!value->gl_context ||
            !SDL_GL_MakeCurrent(value->window, value->gl_context)) return false;
    }
    BongoCatNeoError error = {0};
    char body_path[BONGO_CAT_NEO_PATH_CAP], body_fallback_path[BONGO_CAT_NEO_PATH_CAP];
    char heading_path[BONGO_CAT_NEO_PATH_CAP], heading_fallback_path[BONGO_CAT_NEO_PATH_CAP];
    const char *body_font, *body_fallback, *heading_font, *heading_fallback;
    const nk_rune *ranges = NULL;
    body_font = bongo_cat_neo_ui_system_font(body_path, sizeof(body_path), false);
    body_fallback = bongo_cat_neo_ui_system_font(body_fallback_path,
        sizeof(body_fallback_path), true);
    heading_font = bongo_cat_neo_ui_system_heading_font(heading_path,
        sizeof(heading_path), false);
    heading_fallback = bongo_cat_neo_ui_system_heading_font(heading_fallback_path,
        sizeof(heading_fallback_path), true);
    if (!heading_font) heading_font = body_font;
    if (!heading_fallback) heading_fallback = body_fallback;
    if (value->app->i18n) {
        bongo_cat_neo_i18n_all_glyph_ranges(value->app->i18n, value->glyph_ranges,
            sizeof(value->glyph_ranges) / sizeof(value->glyph_ranges[0]));
        ranges = value->glyph_ranges;
    }
    if (!bongo_cat_neo_ui_init(&value->ui, value->window, body_font,
        body_fallback, heading_font, heading_fallback, ranges, &error)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
        return false;
    }
    bongo_cat_neo_preferences_assets_load(value);
    bongo_cat_neo_platform_trim_memory();
    value->style_theme = -1;
    value->font_language = value->app->config.app.language;
    int pixel_width = 0, pixel_height = 0;
    SDL_GetWindowSizeInPixels(value->window, &pixel_width, &pixel_height);
    SDL_Log("Preferences GL ready: dedicated=%d pixels=%dx%d",
        value->owns_gl_context, pixel_width, pixel_height);
    SDL_GL_SetSwapInterval(1);
    SDL_StartTextInput(value->window);
    value->render_dirty = true;
    bongo_cat_neo_preferences_live_resize_install(value);
    SDL_GL_MakeCurrent(value->app->window, value->app->gl_context);
    return true;
}
BongoCatNeoPreferences *bongo_cat_neo_preferences_create(BongoCatNeoApp *app) {
    if (!app) return NULL;
    BongoCatNeoPreferences *value = calloc(1, sizeof(*value));
    if (value) { value->app = app;
        value->import_dialog = bongo_cat_neo_preferences_import_create();
        if (!value->import_dialog) { free(value); return NULL; } }
    if (value && app->smoke_preference_page >= 0)
        value->page = app->smoke_preference_page;
    return value;
}
void bongo_cat_neo_preferences_show(BongoCatNeoPreferences *value) {
    if (!value) return;
    if (!value->window && !open_window(value)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Preferences failed: %s", SDL_GetError());
        bongo_cat_neo_preferences_close(value);
        return;
    }
    value->render_dirty = true;
    bongo_cat_neo_preferences_render(value);
    bongo_cat_neo_platform_raise_window(value->window);
}
void bongo_cat_neo_preferences_close(BongoCatNeoPreferences *value) {
    if (!value || !value->window) return;
    bongo_cat_neo_preferences_live_resize_uninstall(value);
    bongo_cat_neo_preferences_shortcut_cancel(value);
    SDL_GL_MakeCurrent(value->window, value->gl_context);
    if (value->input_active) bongo_cat_neo_preferences_input_end(value);
    SDL_StopTextInput(value->window);
    bongo_cat_neo_preferences_model_cache_clear(value->app);
    bongo_cat_neo_ui_animations_reset(&value->ui.context);
    bongo_cat_neo_preferences_assets_clear(value);
    bongo_cat_neo_ui_cursor_destroy(&value->ui); bongo_cat_neo_ui_destroy(&value->ui);
    if (value->owns_gl_context && value->gl_context)
        SDL_GL_DestroyContext(value->gl_context);
    SDL_DestroyWindow(value->window);
    value->window = NULL;
    value->gl_context = NULL;
    value->owns_gl_context = false;
    value->native_drag = false;
    value->chrome_dragging = false;
    SDL_GL_MakeCurrent(value->app->window, value->app->gl_context);
    bongo_cat_neo_platform_trim_memory();
    bongo_cat_neo_config_store_flush(value->app);
}
void bongo_cat_neo_preferences_destroy(BongoCatNeoPreferences *value) {
    if (value) { bongo_cat_neo_preferences_close(value);
        bongo_cat_neo_preferences_import_destroy(value->import_dialog);
        free(value); }
}
void bongo_cat_neo_preferences_request_model_import(BongoCatNeoPreferences *value) {
    if (value && !bongo_cat_neo_preferences_import_is_open(value->import_dialog))
        value->import_requested = true; }
bool bongo_cat_neo_preferences_visible(const BongoCatNeoPreferences *value) { return value && value->window; }
void bongo_cat_neo_preferences_input_begin(BongoCatNeoPreferences *value) {
    if (!value || !value->window || value->input_active) return;
    bongo_cat_neo_ui_input_begin(&value->ui);
    value->input_active = true; }
void bongo_cat_neo_preferences_input_end(BongoCatNeoPreferences *value) {
    if (!value || !value->window || !value->input_active) return;
    bongo_cat_neo_ui_input_end(&value->ui);
    value->input_active = false;
}
static Uint32 event_window(const SDL_Event *event) {
    if (event->type >= SDL_EVENT_WINDOW_FIRST && event->type <= SDL_EVENT_WINDOW_LAST)
        return event->window.windowID;
    switch (event->type) {
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: return event->key.windowID;
    case SDL_EVENT_TEXT_INPUT: return event->text.windowID;
    case SDL_EVENT_MOUSE_MOTION: return event->motion.windowID;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
        return event->button.windowID;
    case SDL_EVENT_MOUSE_WHEEL: return event->wheel.windowID;
    case SDL_EVENT_DROP_FILE: return event->drop.windowID;
    default: return 0;
    }
}

static bool chrome_event(BongoCatNeoPreferences *value, const SDL_Event *event) {
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE) {
        if (bongo_cat_neo_preferences_behavior_dialog_active(value)) {
            bongo_cat_neo_preferences_behavior_dialog_close(value);
            value->render_dirty = true;
            return true;
        }
        if (bongo_cat_neo_preferences_remove_dialog_active(value->app)) {
            bongo_cat_neo_preferences_remove_dialog_clear(value->app);
            value->render_dirty = true;
            return true;
        }
        bongo_cat_neo_preferences_close(value);
        return true;
    }
    if (value->native_drag) return false;
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event->button.button == SDL_BUTTON_LEFT) {
        int width = 0;
        SDL_GetWindowSize(value->window, &width, NULL);
        if (!bongo_cat_neo_ui_title_drag_hit(event->button.x, event->button.y,
            (float)width)) return false;
        SDL_GetWindowPosition(value->window, &value->drag_window_x,
            &value->drag_window_y);
        SDL_GetGlobalMouseState(&value->drag_pointer_x, &value->drag_pointer_y);
        value->chrome_dragging = true;
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP &&
        event->button.button == SDL_BUTTON_LEFT && value->chrome_dragging) {
        value->chrome_dragging = false;
        return true;
    }
    if (event->type != SDL_EVENT_MOUSE_MOTION || !value->chrome_dragging)
        return false;
    float pointer_x = 0.0f, pointer_y = 0.0f;
    SDL_GetGlobalMouseState(&pointer_x, &pointer_y);
    SDL_SetWindowPosition(value->window,
        value->drag_window_x + (int)(pointer_x - value->drag_pointer_x),
        value->drag_window_y + (int)(pointer_y - value->drag_pointer_y));
    return true;
}

bool bongo_cat_neo_preferences_event(BongoCatNeoPreferences *value, const SDL_Event *event) {
    if (!value || !event) return false;
    if (bongo_cat_neo_preferences_import_event(value->import_dialog, value->app,
        value->window, event)) { value->render_dirty = value->window != NULL;
        return true; }
    if (!value->window) return false;
    if (event->type == SDL_EVENT_SYSTEM_THEME_CHANGED) {
        value->render_dirty = true; return false;
    }
    if (event_window(event) != SDL_GetWindowID(value->window)) return false;
    if (bongo_cat_neo_preferences_shortcut_event(value, event)) return true;
    if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        bongo_cat_neo_preferences_close(value);
        return true;
    }
    if (event->type == SDL_EVENT_DROP_FILE) {
        bongo_cat_neo_preferences_import_path(value->app, value->window, event->drop.data);
        return true;
    }
    if (chrome_event(value, event)) return true;
    if (!value->input_active) bongo_cat_neo_preferences_input_begin(value);
    bongo_cat_neo_ui_event(&value->ui, event);
    value->render_dirty = true;
    return true;
}

void bongo_cat_neo_preferences_invalidate(BongoCatNeoPreferences *value) {
    if (value) value->render_dirty = true; }
