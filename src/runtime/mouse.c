#include "runtime.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

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

static void reconcile_button(BongoCatApp *app, bool *current, bool pressed,
    const char *parameter) {
    if (*current == pressed) return;
    *current = pressed;
    bongo_cat_live2d_set_parameter(app->live2d, parameter, pressed ? 1.0f : 0.0f);
    if (!pressed) bongo_cat_window_mark_hit_dirty(app);
    app->dirty = true;
}

static void apply_mouse_coordinates(BongoCatApp *app, double x, double y) {
    SDL_Point point = {(int)x, (int)y}; SDL_Rect bounds;
    SDL_DisplayID display = SDL_GetDisplayForPoint(&point);
    if (!display || !SDL_GetDisplayBounds(display, &bounds) ||
        bounds.w <= 0 || bounds.h <= 0) return;
    float x_ratio = (float)((x - bounds.x) / bounds.w);
    float y_ratio = (float)((y - bounds.y) / bounds.h);
    if (x_ratio < 0.0f) x_ratio = 0.0f;
    if (x_ratio > 1.0f) x_ratio = 1.0f;
    if (y_ratio < 0.0f) y_ratio = 0.0f;
    if (y_ratio > 1.0f) y_ratio = 1.0f;
    float drag_x = 1.0f - 2.0f * x_ratio;
    float drag_y = 1.0f - 2.0f * y_ratio;
    if (app->config.model.mouse_mirror) drag_x = -drag_x;
    // ParamMouseX/Y are retained for models that explicitly expose the
    // compatibility parameters. Head/body/eye parameters are driven by the
    // Cubism TargetPoint in NativeModel, just like Bongo Cat Mver.
    set_parameter(app, "ParamMouseX", x_ratio, y_ratio);
    set_parameter(app, "ParamMouseY", x_ratio, y_ratio);
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
    reconcile_button(app, &app->left_mouse_down,
        (buttons & SDL_BUTTON_LMASK) != 0, "ParamMouseLeftDown");
    reconcile_button(app, &app->right_mouse_down,
        (buttons & SDL_BUTTON_RMASK) != 0, "ParamMouseRightDown");
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
    if (app->config.model.ignore_mouse || !moved) return;
    apply_mouse_coordinates(app, target_x, target_y);
}
