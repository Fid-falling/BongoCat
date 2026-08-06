#include "runtime.h"
#include "bongo_cat/file.h"
#include "bongo_cat/mver_pointer.h"
#include "bongo_cat/overlay.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

static void audit_mouse(BongoCatApp *app, double x, double y) {
    if (!app->smoke_input_audit) return;
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), app->data_root, "input-audit.txt")) return;
    FILE *file = bongo_cat_file_open(path, "ab");
    if (!file) return;
    fprintf(file, "mouse x=%.2f y=%.2f\n", x, y);
    fclose(file);
}

void bongo_cat_app_track_hover(BongoCatApp *app, double x, double y) {
    int window_x, window_y, width, height;
    SDL_GetWindowPosition(app->window, &window_x, &window_y);
    SDL_GetWindowSize(app->window, &width, &height);
    bool inside = x >= window_x && x <= window_x + width &&
        y >= window_y && y <= window_y + height;
    app->pointer_known = true;
    app->pointer_x = x;
    app->pointer_y = y;
    bongo_cat_window_schedule_pointer_hit(app);
    if (inside == app->hover_inside) return;
    app->hover_inside = inside;
    app->hover_deadline_ns = inside ? SDL_GetTicksNS() +
        (uint64_t)(app->config.window.hide_delay_seconds * 1000000000.0) : 0;
    if (!inside && app->hover_hidden) {
        bongo_cat_platform_set_opacity(&app->platform,
            app->config.window.opacity_percent / 100.0f);
        app->hover_hidden = false;
        bongo_cat_window_sync_click_through(app);
    }
}

void bongo_cat_app_update_hover(BongoCatApp *app, uint64_t now) {
    if (!app->config.window.hide_on_hover || !app->hover_inside || app->hover_hidden ||
        !app->hover_deadline_ns || now < app->hover_deadline_ns) return;
    bongo_cat_platform_set_opacity(&app->platform, 0.0f);
    app->hover_hidden = true;
    bongo_cat_window_sync_click_through(app);
}

static void set_parameter(BongoCatApp *app, const char *id,
    float x_ratio, float y_ratio) {
    BongoCatParameterRange range;
    if (!bongo_cat_live2d_parameter(app->live2d, id, &range)) return;
    size_t length = strlen(id);
    char axis = length ? id[length - 1] : 'X';
    float value = bongo_cat_mouse_parameter_value(range.minimum, range.maximum,
        x_ratio, y_ratio, axis, app->config.model.mouse_mirror);
    bongo_cat_live2d_set_parameter(app->live2d, id, value);
}

static bool reconcile_button(BongoCatApp *app, bool *current, bool pressed,
    const char *parameter) {
    if (*current == pressed) return false;
    *current = pressed;
    bongo_cat_live2d_set_parameter(app->live2d, parameter,
        pressed ? 1.0f : 0.0f);
    if (!pressed) bongo_cat_window_mark_hit_dirty(app);
    app->dirty = true;
    return true;
}

static bool mver_pointer_bounds(const BongoCatApp *app, SDL_Rect *bounds) {
    const BongoCatLive2DRenderOptions *options = &app->model_render_options;
    if (!options->mver_projection || !bounds) return false;
    if (options->custom_pointer_bounds) {
        *bounds = (SDL_Rect){options->pointer_left, options->pointer_top,
            options->pointer_right - options->pointer_left,
            options->pointer_bottom - options->pointer_top};
        return bounds->w > 0 && bounds->h > 0;
    }
#ifdef _WIN32
    typedef DPI_AWARENESS_CONTEXT (WINAPI *SetThreadDpiAwarenessContextFn)(
        DPI_AWARENESS_CONTEXT);
    FARPROC set_thread_dpi_proc = GetProcAddress(
        GetModuleHandleW(L"user32.dll"), "SetThreadDpiAwarenessContext");
    SetThreadDpiAwarenessContextFn set_thread_dpi = NULL;
    memcpy(&set_thread_dpi, &set_thread_dpi_proc, sizeof(set_thread_dpi));
    DPI_AWARENESS_CONTEXT previous = set_thread_dpi
        ? set_thread_dpi(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE) : NULL;
    RECT desktop = {0};
    bool available = GetWindowRect(GetDesktopWindow(), &desktop) != FALSE;
    if (previous && set_thread_dpi) set_thread_dpi(previous);
    if (available && desktop.right > 0 && desktop.bottom > 0) {
        // Mver 0.1.6 stores right/bottom directly and uses a zero origin.
        *bounds = (SDL_Rect){0, 0, desktop.right, desktop.bottom};
        return true;
    }
#endif
    SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    SDL_Rect display;
    if (!primary || !SDL_GetDisplayBounds(primary, &display)) return false;
    *bounds = (SDL_Rect){0, 0, display.w, display.h};
    return bounds->w > 0 && bounds->h > 0;
}

static bool mver_model_pointer(BongoCatApp *app, double absolute_x,
    double absolute_y, double *x, double *y, bool *changed) {
    SDL_Rect bounds;
    if (!mver_pointer_bounds(app, &bounds)) return false;
    BongoCatMverPointerBounds pointer_bounds = {
        bounds.x, bounds.y, bounds.w, bounds.h
    };
    double relative_x = 0.0, relative_y = 0.0;
    bool use_relative = app->model_render_options.mouse_force_move &&
        bongo_cat_platform_relative_pointer(&app->platform,
            &relative_x, &relative_y);
    bool initialized = app->mver_pointer.initialized;
    double previous_x = app->mver_pointer.x;
    double previous_y = app->mver_pointer.y;
    if (!bongo_cat_mver_pointer_update(&app->mver_pointer,
        absolute_x, absolute_y, relative_x, relative_y, use_relative,
        &pointer_bounds, x, y)) return false;
    *changed = !initialized || previous_x != *x || previous_y != *y;
    return true;
}

static void apply_mouse_coordinates(BongoCatApp *app, double x, double y) {
    SDL_Point point = {(int)x, (int)y}; SDL_Rect bounds;
    if (!mver_pointer_bounds(app, &bounds)) {
        SDL_DisplayID display = SDL_GetDisplayForPoint(&point);
        if (!display || !SDL_GetDisplayBounds(display, &bounds)) return;
    }
    BongoCatMverPointerBounds pointer_bounds = {
        bounds.x, bounds.y, bounds.w, bounds.h
    };
    float x_ratio, y_ratio;
    if (!bongo_cat_mver_pointer_ratios(x, y, &pointer_bounds,
        &x_ratio, &y_ratio)) return;
    bool exact_pointer = bongo_cat_overlay_mver_pointer_enabled(app->overlay);
    bool mver = app->model_render_options.mver_projection;
    bool left_handed = app->model_render_options.pointer_left_handed ||
        (exact_pointer && bongo_cat_overlay_mver_pointer_left_handed(app->overlay));
    float mver_x = left_handed ? 1.0f - x_ratio : x_ratio;
    float drag_x = mver ? 2.0f * mver_x - 1.0f :
        1.0f - 2.0f * x_ratio;
    float drag_y = 1.0f - 2.0f * y_ratio;
    if (!mver && app->config.model.mouse_mirror) drag_x = -drag_x;
    bongo_cat_overlay_set_mver_pointer(app->overlay, x_ratio, y_ratio,
        app->left_mouse_down, app->right_mouse_down, app->side_mouse_down);
    if (app->model_render_options.mver_projection && !exact_pointer) {
        // Mver-authored hand and pen deformation uses the complete extension
        // range alongside TargetPoint; attenuating it visibly shortens travel.
        set_parameter(app, "ParamMouseX", 1.0f - x_ratio, y_ratio);
        set_parameter(app, "ParamMouseY", x_ratio, y_ratio);
    } else if (!app->model_render_options.mver_projection) {
        // Standalone models retain the full authored extension range.
        set_parameter(app, "ParamMouseX", x_ratio, y_ratio);
        set_parameter(app, "ParamMouseY", x_ratio, y_ratio);
    }
    bongo_cat_live2d_set_dragging(app->live2d, drag_x, drag_y);
    app->dirty = true;
}

void bongo_cat_app_apply_mouse_position(BongoCatApp *app, double x, double y,
    float elapsed_seconds) {
    if (!app || app->config.model.ignore_mouse) return;
    (void)elapsed_seconds;
    apply_mouse_coordinates(app, x, y);
}

void bongo_cat_app_apply_mouse(BongoCatApp *app) {
    if (!app) return;
    double target_x, target_y;
    bool received = bongo_cat_input_take_mouse(&app->input, &target_x, &target_y);
    float global_x = 0.0f, global_y = 0.0f;
    SDL_MouseButtonFlags buttons = SDL_GetGlobalMouseState(&global_x, &global_y);
    bool button_changed = reconcile_button(app, &app->left_mouse_down,
        (buttons & SDL_BUTTON_LMASK) != 0, "ParamMouseLeftDown");
    button_changed = reconcile_button(app, &app->right_mouse_down,
        (buttons & SDL_BUTTON_RMASK) != 0, "ParamMouseRightDown") || button_changed;
    bool side_down = (buttons & (SDL_BUTTON_X1MASK | SDL_BUTTON_X2MASK)) != 0;
    if (app->side_mouse_down != side_down) {
        app->side_mouse_down = side_down;
        button_changed = true;
        app->dirty = true;
    }
    if (!received || target_x != global_x || target_y != global_y) {
        target_x = global_x;
        target_y = global_y;
    }
    bool moved = !app->pointer_known || app->pointer_x != target_x ||
        app->pointer_y != target_y;
    if (moved) {
        audit_mouse(app, target_x, target_y);
        bongo_cat_app_track_hover(app, target_x, target_y);
    }
    bongo_cat_window_sync_click_through(app);
    uint64_t now = SDL_GetTicksNS();
    app->mouse_last_ns = now;
    double model_x = target_x, model_y = target_y;
    bool model_moved = moved;
    if (app->model_render_options.mver_projection &&
        !mver_model_pointer(app, target_x, target_y,
            &model_x, &model_y, &model_moved)) return;
    if (app->config.model.ignore_mouse || (!model_moved && !button_changed)) return;
    apply_mouse_coordinates(app, model_x, model_y);
}
