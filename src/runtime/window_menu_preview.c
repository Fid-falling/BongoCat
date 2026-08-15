#include "window_menu.h"

#include <stdio.h>
#include <string.h>

#define MODEL_PREVIEW_DELAY_NS 150000000ull

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
    if (action >= BONGO_CAT_MENU_MOTION_FIRST &&
        action < BONGO_CAT_MENU_MOTION_FIRST + BONGO_CAT_BEHAVIOR_CAP) return 4;
    if (action >= BONGO_CAT_MENU_EXPRESSION_FIRST &&
        action < BONGO_CAT_MENU_EXPRESSION_FIRST + BONGO_CAT_BEHAVIOR_CAP) return 5;
    return 0;
}

void bongo_cat_window_menu_preview_init(BongoCatWindowMenuPreview *state,
    BongoCatApp *app) {
    if (!state) return;
    *state = (BongoCatWindowMenuPreview){.app = app,
        .scale = app ? app->config.window.scale_percent : 100.0f,
        .opacity = app ? app->config.window.opacity_percent : 100.0f,
        .expression = app ? bongo_cat_live2d_expression(app->live2d) : -1,
        .last = BONGO_CAT_MENU_NONE, .applied = BONGO_CAT_MENU_NONE,
        .pending_model = BONGO_CAT_MENU_NONE};
    bongo_cat_modal_frame_init(&state->modal_frame, app);
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
    if (menu_model(app, action)) {
        state->pending_model = action;
        state->pending_model_since_ns = SDL_GetTicksNS();
        return;
    }
    state->pending_model = BONGO_CAT_MENU_NONE;
    if (action >= BONGO_CAT_MENU_SCALE_50 &&
        action <= BONGO_CAT_MENU_SCALE_200)
        bongo_cat_window_set_scale(app,
            (float)(50 + 10 * (action - BONGO_CAT_MENU_SCALE_50)));
    else if (action >= BONGO_CAT_MENU_OPACITY_10 &&
        action <= BONGO_CAT_MENU_OPACITY_100) {
        app->config.window.opacity_percent =
            (float)(10 * (action - BONGO_CAT_MENU_OPACITY_10 + 1));
        bongo_cat_platform_set_opacity(&app->platform,
            app->config.window.opacity_percent / 100.0f);
    } else if (action == state->applied &&
        bongo_cat_window_behavior_menu_action(action)) {
        bongo_cat_modal_frame_tick(&state->modal_frame);
        return;
    } else if (bongo_cat_window_behavior_preview(app, action)) {
        state->applied = action;
        bongo_cat_modal_frame_tick(&state->modal_frame);
        return;
    }
    bongo_cat_app_render_now(app);
}

void bongo_cat_window_menu_preview_tick(void *userdata) {
    BongoCatWindowMenuPreview *state = userdata;
    if (!state) return;
    if (state->pending_model != BONGO_CAT_MENU_NONE &&
        SDL_GetTicksNS() - state->pending_model_since_ns >=
            MODEL_PREVIEW_DELAY_NS) {
        BongoCatMenuAction action = state->pending_model;
        state->pending_model = BONGO_CAT_MENU_NONE;
        const BongoCatModelEntry *model = action == state->last ?
            menu_model(state->app, action) : NULL;
        if (model) {
            BongoCatError error = {0};
            if (!bongo_cat_app_select_model_with_error(state->app, model->id,
                &error)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Model preview failed: %s",
                    error.message[0] ? error.message : "unknown error");
                if (strcmp(state->app->config.current_model,
                    state->model) != 0) {
                    BongoCatError restore_error = {0};
                    if (!bongo_cat_app_select_model_with_error(state->app,
                        state->model, &restore_error))
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Model preview recovery failed: %s",
                            restore_error.message[0] ?
                                restore_error.message : "unknown error");
                }
            }
        }
    }
    bongo_cat_modal_frame_tick(&state->modal_frame);
}

void bongo_cat_window_menu_restore(void *userdata, BongoCatMenuAction selected) {
    BongoCatWindowMenuPreview *state = userdata;
    if (!state || !state->app) return;
    BongoCatApp *app = state->app;
    state->pending_model = BONGO_CAT_MENU_NONE;
    bool changed = false;
    bool keep_motion = selected >= BONGO_CAT_MENU_MOTION_FIRST &&
        selected < BONGO_CAT_MENU_MOTION_FIRST + BONGO_CAT_BEHAVIOR_CAP;
    bool committed_motion = keep_motion &&
        bongo_cat_window_behavior_commit_preview(app, selected);
    if (!committed_motion && bongo_cat_live2d_restore_motion_preview(app->live2d)) {
        state->applied = BONGO_CAT_MENU_NONE;
        changed = true;
    }
    if (committed_motion) { state->applied = selected; changed = true; }
    if (selected == BONGO_CAT_MENU_NONE)
        state->applied = BONGO_CAT_MENU_NONE;
    bool keep_model = menu_model(app, selected) != NULL;
    bool keep_scale = selected >= BONGO_CAT_MENU_SCALE_50 &&
        selected <= BONGO_CAT_MENU_SCALE_200;
    bool keep_opacity = selected >= BONGO_CAT_MENU_OPACITY_10 &&
        selected <= BONGO_CAT_MENU_OPACITY_100;
    /* Expressions belong to the model active when the menu opened. */
    bool keep_expression = keep_model ||
        (selected >= BONGO_CAT_MENU_EXPRESSION_FIRST &&
        selected < BONGO_CAT_MENU_EXPRESSION_FIRST + BONGO_CAT_BEHAVIOR_CAP);
    if (!keep_model && strcmp(app->config.current_model, state->model) != 0) {
        BongoCatError error = {0};
        if (bongo_cat_app_select_model_with_error(app, state->model, &error))
            changed = true;
        else SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Model preview restore failed: %s",
            error.message[0] ? error.message : "unknown error");
    }
    if (!keep_scale &&
        SDL_fabsf(app->config.window.scale_percent - state->scale) > .01f) {
        bongo_cat_window_set_scale(app, state->scale);
        changed = true;
    }
    if (!keep_opacity &&
        SDL_fabsf(app->config.window.opacity_percent - state->opacity) > .01f) {
        app->config.window.opacity_percent = state->opacity;
        bongo_cat_platform_set_opacity(&app->platform,
            state->opacity / 100.0f);
        changed = true;
    }
    if (!keep_expression &&
        bongo_cat_live2d_expression(app->live2d) != state->expression &&
        bongo_cat_live2d_set_expression(app->live2d, state->expression)) {
        bongo_cat_app_step_live2d(app, 1.0f / 60.0f);
        changed = true;
    }
    if (changed) bongo_cat_app_render_now(app);
}

bool bongo_cat_window_menu_preview_applied(
    const BongoCatWindowMenuPreview *state, BongoCatMenuAction selected) {
    return state && bongo_cat_window_behavior_menu_action(selected) &&
        state->applied == selected;
}
