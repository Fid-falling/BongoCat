#include "runtime.h"
#include "bongo_cat/preferences.h"

#include <SDL3/SDL_opengl.h>

bool bongo_cat_window_visible_at_pointer(BongoCatApp *app, float x, float y) {
    int width, height, pixel_width, pixel_height;
    if (!SDL_GetWindowSize(app->window, &width, &height) ||
        !SDL_GetWindowSizeInPixels(app->window, &pixel_width, &pixel_height) ||
        width <= 0 || height <= 0 || pixel_width <= 0 || pixel_height <= 0) return false;
    int pixel_x = SDL_clamp((int)(x * pixel_width / width), 0, pixel_width - 1);
    int pixel_y = pixel_height - 1 -
        SDL_clamp((int)(y * pixel_height / height), 0, pixel_height - 1);
    uint8_t presented_alpha = 0;
    if (bongo_cat_platform_frame_alpha(&app->platform, pixel_width, pixel_height,
        pixel_x, pixel_y, &presented_alpha)) return presented_alpha > 8;
    SDL_Window *previous_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
    if (!SDL_GL_MakeCurrent(app->window, app->gl_context)) return false;
    GLint previous_buffer;
    GLubyte pixel[4] = {0};
    glGetIntegerv(GL_READ_BUFFER, &previous_buffer);
    glReadBuffer(GL_FRONT);
    glReadPixels(pixel_x, pixel_y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[3] <= 8) {
        glReadBuffer(GL_BACK);
        glReadPixels(pixel_x, pixel_y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    }
    glReadBuffer((GLenum)previous_buffer);
    if (previous_window && previous_context)
        SDL_GL_MakeCurrent(previous_window, previous_context);
    return pixel[3] > 8;
}

void bongo_cat_window_mark_hit_dirty(BongoCatApp *app) {
    if (!app) return;
    app->pointer_hit_dirty = true;
    app->pointer_hit_deadline_ns = 0;
}

void bongo_cat_window_set_visible(BongoCatApp *app, bool visible) {
    if (!app || !app->window) return;
    app->config.window.visible = visible;
    if (!visible) {
        bongo_cat_platform_set_visible(&app->platform, false);
        return;
    }
    if (SDL_GetWindowFlags(app->window) & SDL_WINDOW_MINIMIZED)
        SDL_RestoreWindow(app->window);
    app->window_minimized = false;
    app->hover_hidden = false;
    bongo_cat_platform_set_opacity(&app->platform,
        app->config.window.opacity_percent / 100.0f);
    bongo_cat_platform_set_visible(&app->platform, true);
    if (app->config.window.keep_in_screen) bongo_cat_window_clamp_to_display(app);
    else bongo_cat_window_recover_to_display(app);
    bongo_cat_window_mark_hit_dirty(app);
    app->dirty = true;
}

void bongo_cat_window_schedule_pointer_hit(BongoCatApp *app) {
    if (!app) return;
    uint64_t deadline = SDL_GetTicksNS() + 8000000ull;
    if (!app->pointer_hit_dirty) {
        app->pointer_hit_dirty = true;
        app->pointer_hit_deadline_ns = deadline;
    } else if (app->pointer_hit_deadline_ns &&
        app->pointer_hit_deadline_ns > deadline) {
        app->pointer_hit_deadline_ns = deadline;
    }
}

void bongo_cat_window_schedule_hit_check(BongoCatApp *app) {
    if (!app || app->pointer_hit_dirty || !app->pointer_known) return;
    app->pointer_hit_dirty = true;
    app->pointer_hit_deadline_ns = SDL_GetTicksNS() + 100000000ull;
}

void bongo_cat_window_sync_click_through(BongoCatApp *app) {
    if (!app || !app->window) return;
    bool forced = app->config.window.pass_through || app->hover_hidden;
    if (!forced && !bongo_cat_platform_dynamic_hit_supported()) {
        app->pointer_transparent = false;
        app->pointer_hit_dirty = false;
    }
    if (!forced && (app->left_mouse_down || app->right_mouse_down)) return;
    if (!forced && app->config.window.visible && app->pointer_known &&
        app->pointer_hit_dirty &&
        (!app->pointer_hit_deadline_ns || SDL_GetTicksNS() >= app->pointer_hit_deadline_ns)) {
        float local_x, local_y;
        bool inside = bongo_cat_platform_pointer_local(&app->platform,
            app->pointer_x, app->pointer_y, &local_x, &local_y);
        app->pointer_transparent = inside && !bongo_cat_window_visible_at_pointer(app,
            local_x, local_y);
        app->pointer_hit_dirty = false;
        app->pointer_hit_deadline_ns = 0;
    }
    bool enabled = forced || (app->pointer_known && app->pointer_transparent);
    if (app->click_through_valid && app->click_through_applied == enabled) return;
    bongo_cat_platform_set_click_through(&app->platform, enabled);
    app->click_through_applied = enabled;
    app->click_through_valid = true;
    app->dirty = true;
}

void bongo_cat_window_apply_pending_resize(BongoCatApp *app) {
    if (!app || !app->resize_pending) return;
    if (app->wheel_animation_active) {
        bongo_cat_live2d_reshape(app->live2d,
            app->resize_pixel_width, app->resize_pixel_height);
        return;
    }
    app->resize_pending = false;
    bongo_cat_live2d_resize(app->live2d,
        app->resize_pixel_width, app->resize_pixel_height);
    bongo_cat_window_mark_hit_dirty(app);
}
