#include "window_menu.h"

#include <stdio.h>
#include <string.h>

static const BongoCatModelEntry *menu_model(const BongoCatApp *app,
    BongoCatMenuAction action) {
    if (!app || action < BONGO_CAT_MENU_MODEL_FIRST ||
        action >= BONGO_CAT_MENU_MODEL_FIRST + BONGO_CAT_MODEL_CAP) return NULL;
    size_t index = (size_t)(action - BONGO_CAT_MENU_MODEL_FIRST);
    return index < app->models.count ? &app->models.entries[index] : NULL;
}

static bool previewable(const BongoCatApp *app, BongoCatMenuAction action) {
    return (action >= BONGO_CAT_MENU_SCALE_50 &&
        action <= BONGO_CAT_MENU_OPACITY_100) ||
        menu_model(app, action) != NULL ||
        bongo_cat_window_behavior_menu_action(action);
}

static int preview_group(const BongoCatApp *app, BongoCatMenuAction action) {
    if (action >= BONGO_CAT_MENU_SCALE_50 &&
        action <= BONGO_CAT_MENU_SCALE_200) return 1;
    if (action >= BONGO_CAT_MENU_OPACITY_10 &&
        action <= BONGO_CAT_MENU_OPACITY_100) return 2;
    if (menu_model(app, action)) return 3;
    if (bongo_cat_window_behavior_menu_action(action)) return 4;
    return 0;
}

void bongo_cat_window_menu_preview_init(BongoCatWindowMenuPreview *state,
    BongoCatApp *app) {
    if (!state) return;
    *state = (BongoCatWindowMenuPreview){app, "",
        app ? app->config.window.scale_percent : 100.0f,
        app ? app->config.window.opacity_percent : 100.0f,
        BONGO_CAT_MENU_NONE, SDL_GetTicksNS()};
    if (app) snprintf(state->model, sizeof(state->model), "%.*s",
        (int)sizeof(state->model) - 1, app->config.current_model);
}

void bongo_cat_window_menu_preview(void *userdata, BongoCatMenuAction action) {
    BongoCatWindowMenuPreview *state = userdata;
    if (!state || !state->app || action == state->last) return;
    BongoCatApp *app = state->app;
    int previous_group = preview_group(app, state->last);
    int next_group = preview_group(app, action);
    if (state->last != BONGO_CAT_MENU_NONE && previous_group != next_group)
        bongo_cat_window_menu_restore(state, BONGO_CAT_MENU_NONE);
    if (!previewable(app, action)) {
        if (previous_group == 0)
            bongo_cat_window_menu_restore(state, BONGO_CAT_MENU_NONE);
        state->last = action;
        return;
    }
    state->last = action;
    const BongoCatModelEntry *model = menu_model(app, action);
    if (model) bongo_cat_app_select_model(app, model->id);
    else if (action >= BONGO_CAT_MENU_SCALE_50 &&
        action <= BONGO_CAT_MENU_SCALE_200)
        bongo_cat_window_set_scale(app,
            (float)(50 + 10 * (action - BONGO_CAT_MENU_SCALE_50)));
    else if (action >= BONGO_CAT_MENU_OPACITY_10 &&
        action <= BONGO_CAT_MENU_OPACITY_100) {
        app->config.window.opacity_percent =
            (float)(10 * (action - BONGO_CAT_MENU_OPACITY_10 + 1));
        SDL_SetWindowOpacity(app->window, app->config.window.opacity_percent / 100.0f);
    } else if (bongo_cat_window_behavior_action(app, action)) {
        // Native menu tracking pauses the main loop, so advance once here to
        // expose the first motion/expression frame while hovering.
        bongo_cat_live2d_update(app->live2d, 1.0f / 60.0f);
        state->last_tick_ns = app->last_frame_ns = SDL_GetTicksNS();
    }
    bongo_cat_app_render_now(app);
}

void bongo_cat_window_menu_preview_tick(void *userdata) {
    BongoCatWindowMenuPreview *state = userdata;
    if (!state || !state->app) return;
    uint64_t now = SDL_GetTicksNS();
    float elapsed = (float)((now - state->last_tick_ns) / 1000000000.0);
    if (elapsed > 0.25f) elapsed = 0.25f;
    state->last_tick_ns = state->app->last_frame_ns = now;
    if (bongo_cat_live2d_update(state->app->live2d, elapsed))
        bongo_cat_app_render_now(state->app);
}

void bongo_cat_window_menu_restore(void *userdata, BongoCatMenuAction selected) {
    BongoCatWindowMenuPreview *state = userdata;
    if (!state || !state->app) return;
    BongoCatApp *app = state->app;
    bool changed = false;
    bool keep_model = menu_model(app, selected) != NULL;
    bool keep_scale = selected >= BONGO_CAT_MENU_SCALE_50 &&
        selected <= BONGO_CAT_MENU_SCALE_200;
    bool keep_opacity = selected >= BONGO_CAT_MENU_OPACITY_10 &&
        selected <= BONGO_CAT_MENU_OPACITY_100;
    if (!keep_model && strcmp(app->config.current_model, state->model) != 0) {
        bongo_cat_app_select_model(app, state->model);
        changed = true;
    }
    if (!keep_scale &&
        SDL_fabsf(app->config.window.scale_percent - state->scale) > .01f) {
        bongo_cat_window_set_scale(app, state->scale);
        changed = true;
    }
    if (!keep_opacity &&
        SDL_fabsf(app->config.window.opacity_percent - state->opacity) > .01f) {
        app->config.window.opacity_percent = state->opacity;
        SDL_SetWindowOpacity(app->window, state->opacity / 100.0f);
        changed = true;
    }
    if (changed) bongo_cat_app_render_now(app);
}
