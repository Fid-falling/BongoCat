#include "runtime.h"
#include "bongo_cat/preferences.h"

#include <SDL3/SDL_opengl.h>
#include <stdlib.h>

bool bongo_cat_window_visible_at_pointer(BongoCatApp *app, float x, float y) {
    int width, height, pixel_width, pixel_height;
    if (!SDL_GetWindowSize(app->window, &width, &height) ||
        !SDL_GetWindowSizeInPixels(app->window, &pixel_width, &pixel_height) ||
        width <= 0 || height <= 0 || pixel_width <= 0 || pixel_height <= 0) return false;
    int pixel_x = SDL_clamp((int)(x * pixel_width / width), 0, pixel_width - 1);
    int pixel_y = pixel_height - 1 -
        SDL_clamp((int)(y * pixel_height / height), 0, pixel_height - 1);
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
        SDL_HideWindow(app->window);
        return;
    }
    app->hover_hidden = false;
    SDL_SetWindowOpacity(app->window,
        app->config.window.opacity_percent / 100.0f);
    SDL_ShowWindow(app->window);
    bongo_cat_window_clamp_to_display(app);
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

int bongo_cat_window_wait_timeout(const BongoCatApp *app, uint64_t now) {
    if (!app) return 250;
    int wait_ms = app->config.window.visible ? BONGO_CAT_FRAME_WAIT(app) : 250;
    if (bongo_cat_preferences_needs_frame(app->preferences) && wait_ms > 4)
        wait_ms = 4;
    if (app->wheel_animation_active && wait_ms > 8) wait_ms = 8;
    bool pending_hit = app->config.window.visible && app->pointer_hit_dirty &&
        app->pointer_hit_deadline_ns &&
        !app->config.window.pass_through && !app->hover_hidden &&
        !app->left_mouse_down && !app->right_mouse_down;
    if (!pending_hit) return wait_ms;
    if (app->pointer_hit_deadline_ns <= now) return 0;
    uint64_t remaining_ns = app->pointer_hit_deadline_ns - now;
    uint64_t remaining_ms = remaining_ns / 1000000ull +
        (remaining_ns % 1000000ull != 0);
    return remaining_ms < (uint64_t)wait_ms ? (int)remaining_ms : wait_ms;
}

bool bongo_cat_window_wait_timeout_self_test(void) {
    const uint64_t now = 1000000000ull;
    BongoCatApp *app = calloc(1, sizeof(*app));
    if (!app) return false;
    bool passed = false;
    app->config.window.visible = true;
    app->config.model.max_fps = 60;
    app->pointer_hit_dirty = true;
    app->pointer_hit_deadline_ns = now + 8000000ull;
    if (bongo_cat_window_wait_timeout(app, now) != 8) goto done;
    app->config.model.max_fps = 30;
    if (bongo_cat_window_wait_timeout(app, now) != 8) goto done;
    app->pointer_hit_deadline_ns = now + 8500000ull;
    if (bongo_cat_window_wait_timeout(app, now) != 9) goto done;
    app->pointer_hit_deadline_ns = now;
    if (bongo_cat_window_wait_timeout(app, now) != 0) goto done;
    app->pointer_hit_dirty = false;
    if (bongo_cat_window_wait_timeout(app, now) != BONGO_CAT_FRAME_WAIT(app)) goto done;
    app->pointer_hit_dirty = true;
    app->pointer_hit_deadline_ns = now + 8000000ull;
    app->config.window.pass_through = true;
    if (bongo_cat_window_wait_timeout(app, now) != BONGO_CAT_FRAME_WAIT(app)) goto done;
    app->config.window.pass_through = false;
    app->left_mouse_down = true;
    if (bongo_cat_window_wait_timeout(app, now) != BONGO_CAT_FRAME_WAIT(app)) goto done;
    app->left_mouse_down = false;
    app->config.window.visible = false;
    if (bongo_cat_window_wait_timeout(app, now) != 250) goto done;
    app->pointer_hit_dirty = false;
    if (bongo_cat_window_wait_timeout(app, now) != 250) goto done;
    app->wheel_animation_active = true;
    passed = bongo_cat_window_wait_timeout(app, now) == 8;
done:
    free(app);
    return passed;
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
