#include "runtime.h"
#include "model_import.h"
#include "bongo_cat/audio.h"
#include "bongo_cat/overlay.h"
#include "bongo_cat/preferences.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void select_model_state(BongoCatApp *app, const BongoCatModelEntry *entry) {
    snprintf(app->session.active_model_id, sizeof(app->session.active_model_id), "%s",
        entry->id);
    app->loaded_mode = entry->mode;
    bongo_cat_gamepads_set_enabled(app, entry->mode == BONGO_CAT_MODE_GAMEPAD);
}
static void request_model_frame(BongoCatApp *app, bool reveal) {
    if (!app) return;
    if (app->window) {
        SDL_WindowFlags flags = SDL_GetWindowFlags(app->window);
        bool hidden = (flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) != 0;
        if (reveal && (!app->session.window.visible || hidden ||
            app->hover_hidden))
            bongo_cat_window_set_visible(app, true);
        else if (app->session.window.visible)
            bongo_cat_window_clamp_to_display(app);
    }
    bongo_cat_window_mark_hit_dirty(app);
    app->dirty = true;
}

static void model_runtime_stage(BongoCatApp *app, const char *stage,
    const BongoCatModelEntry *entry) {
    char state[BONGO_CAT_ID_CAP + 32];
    const char *id = entry ? entry->id : "unknown";
    snprintf(state, sizeof(state), "model-load:%s:%s", stage, id);
    bongo_cat_runtime_stage(app, state);
    SDL_Log("[runtime] Model load %s: id=%s", stage, id);
}

static void model_progress_runtime_stage(BongoCatApp *app, float progress) {
    static const char *names[] = {"", "model-core", "texture-loading",
        "finalizing"};
    unsigned stage = progress >= .95f ? 3 : progress >= .50f ? 2 : 1;
    if (!app || stage == app->model_load_runtime_stage) return;
    app->model_load_runtime_stage = stage;
    char state[BONGO_CAT_ID_CAP + 40];
    snprintf(state, sizeof(state), "model-load:%s:%s", names[stage],
        app->loading_model[0] ? app->loading_model : "unknown");
    bongo_cat_runtime_stage(app, state);
}

static void model_load_progress(void *userdata, float progress) {
    BongoCatApp *app = userdata;
    model_progress_runtime_stage(app, progress);
    if (app && app->preferences)
        bongo_cat_preferences_model_load_progress(app->preferences, progress);
    /* The main loop is blocked during loading, so advance the visible model here. */
    if (!app || !app->loaded_model[0] || !app->model_load_last_frame_ns ||
        !app->live2d)
        return;
    uint64_t now = SDL_GetTicksNS();
    if (progress < .98f && now - app->model_load_last_frame_ns < 16000000ull)
        return;
    uint64_t previous = app->model_load_last_frame_ns;
    app->model_load_last_frame_ns = now;
    float elapsed = (float)((now - previous) / 1000000000.0);
    bongo_cat_app_drain_input(app, false);
    bongo_cat_app_update_hover(app, now);
    if (!app->smoke_freeze_model && elapsed > 0.0f)
        bongo_cat_app_step_live2d(app, elapsed);
    app->last_frame_ns = now;
    bongo_cat_app_render_now(app);
}

static bool apply_model_aspect(BongoCatApp *app,
    const BongoCatLive2DRenderOptions *options) {
    if (!app || !app->window) return false;
    int reference_width = options && options->mver_projection
        ? options->reference_width : 612;
    int reference_height = options && options->mver_projection
        ? options->reference_height : 354;
    int x, y, width, height;
    if (reference_width <= 0 || reference_height <= 0 ||
        !SDL_GetWindowPosition(app->window, &x, &y) ||
        !SDL_GetWindowSize(app->window, &width, &height) || height <= 0)
        return false;
    int next_width = (int)((double)height * reference_width /
        reference_height + 0.5);
    if (next_width < 64) next_width = 64;
    if (next_width > 8192) next_width = 8192;
    if (next_width == width) return false;
    return bongo_cat_window_apply_geometry(app, x, y,
        app->session.window.scale_percent, next_width, height);
}

static void commit_model(BongoCatApp *app,
    const BongoCatModelEntry *entry, bool force_refresh, bool reveal) {
    bool changed = strcmp(app->session.active_model_id, entry->id) != 0 ||
        app->loaded_mode != entry->mode;
    if (!changed && !force_refresh) return;
    select_model_state(app, entry);
    app->model_selection_serial++;
    request_model_frame(app, reveal);
}

bool bongo_cat_app_select_model_with_error(BongoCatApp *app,
    const char *id, BongoCatError *error) {
    BongoCatError local = {0};
    BongoCatError *failure = error ? error : &local;
    *failure = (BongoCatError){0};
    if (!app || !app->live2d || !id) {
        bongo_cat_error_set(failure, BONGO_CAT_ERROR_ARGUMENT,
            "Cannot select a model without an active renderer and model id");
        return false;
    }
    const BongoCatModelEntry *entry = bongo_cat_models_find(&app->models, id);
    if (!entry) {
        bongo_cat_error_set(failure, BONGO_CAT_ERROR_ARGUMENT,
            "Model is not installed: %s", id);
        return false;
    }
    if (app->loaded_model[0] && strcmp(app->loaded_model, entry->id) == 0) {
        commit_model(app, entry, false, false);
        return true;
    }
    bool replacing_model = app->loaded_model[0] != '\0';
    BongoCatError optional = {0};
    BongoCatBehaviorCatalog *behaviors = calloc(1, sizeof(*behaviors));
    if (!behaviors) {
        bongo_cat_error_set(failure, BONGO_CAT_ERROR_MEMORY,
            "Cannot allocate model behavior state");
        return false;
    }
    if (bongo_cat_behaviors_load(behaviors, entry, &optional) != BONGO_CAT_OK)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", optional.message);
    BongoCatLive2DRenderOptions render_options = {0};
    bool adapted_profile = bongo_cat_import_render_options(
        entry->adapter_directory, &render_options);
    if (adapted_profile) SDL_Log("Imported runtime profile: projection=%.4f "
        "force_mouse=%d left_handed=%d pointer_bounds=%d",
        render_options.projection_scale, render_options.mouse_force_move,
        render_options.pointer_left_handed, render_options.custom_pointer_bounds);
    int pixel_width = app->session.window.width, pixel_height = app->session.window.height;
    if (app->window) SDL_GetWindowSizeInPixels(app->window, &pixel_width, &pixel_height);
    SDL_Window *previous_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
    bool restore_context = previous_window != app->window ||
        previous_context != app->gl_context;
    if (restore_context && !SDL_GL_MakeCurrent(app->window, app->gl_context)) {
        bongo_cat_error_set(failure, BONGO_CAT_ERROR_PLATFORM,
            "Cannot activate the main OpenGL context: %s", SDL_GetError());
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", failure->message);
        free(behaviors);
        return false;
    }
    bongo_cat_live2d_reshape(app->live2d, pixel_width, pixel_height);
    snprintf(app->loading_model, sizeof(app->loading_model), "%s", entry->id);
    app->model_load_runtime_stage = 0;
    model_runtime_stage(app, "started", entry);
    app->model_load_last_frame_ns = replacing_model ? SDL_GetTicksNS() : 0;
    BongoCatResult load_result = bongo_cat_live2d_load(app->live2d, entry->directory,
        entry->setting_file, entry->preset, &render_options,
        model_load_progress, app, failure);
    app->model_load_last_frame_ns = 0;
    if (replacing_model) app->last_frame_ns = SDL_GetTicksNS();
    if (load_result != BONGO_CAT_OK) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Model load failed: id=%s error=%s", entry->id, failure->message);
        if (restore_context && previous_window && previous_context &&
            !SDL_GL_MakeCurrent(previous_window, previous_context))
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "Cannot restore the previous OpenGL context: %s", SDL_GetError());
        free(behaviors);
        if (!bongo_cat_live2d_ready(app->live2d)) app->loaded_model[0] = '\0';
        request_model_frame(app, false);
        app->loading_model[0] = '\0';
        app->model_load_runtime_stage = 0;
        bongo_cat_runtime_stage(app, "running");
        return false;
    }
    model_runtime_stage(app, "committing", entry);
    BongoCatParameterRange pointer_x, pointer_y;
    bool model_pointer =
        bongo_cat_live2d_parameter(app->live2d, "ParamMouseX", &pointer_x) &&
        bongo_cat_live2d_parameter(app->live2d, "ParamMouseY", &pointer_y) &&
        pointer_x.maximum > pointer_x.minimum &&
        pointer_y.maximum > pointer_y.minimum;
    app->model_render_options = render_options;
    bongo_cat_app_reset_pointer_tracking(app);
    bongo_cat_live2d_set_render_options(app->live2d, &render_options);
    app->behaviors = *behaviors;
    free(behaviors);
    optional = (BongoCatError){0};
    if (bongo_cat_overlay_load(app->overlay, entry->adapter_directory,
        model_pointer, &optional) != BONGO_CAT_OK) {
        if (optional.message[0])
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", optional.message);
        bongo_cat_overlay_clear(app->overlay);
    }
    snprintf(app->loaded_model, sizeof(app->loaded_model), "%s", entry->id);
    app->pointer_known = false;
    bool geometry_changed = apply_model_aspect(app, &render_options);
    if (app->window) {
        if (geometry_changed) SDL_SyncWindow(app->window);
        SDL_GetWindowSizeInPixels(app->window, &pixel_width, &pixel_height);
    }
    bongo_cat_live2d_resize(app->live2d, pixel_width, pixel_height);
    bongo_cat_audio_stop(app->audio);
    commit_model(app, entry, true, replacing_model);
    bongo_cat_app_reapply_input(app);
    bongo_cat_app_apply_mouse(app);
    /* Do not leave the previous frame cropped during the UI completion pass. */
    if (replacing_model) bongo_cat_app_render_now(app);
    if (restore_context && previous_window && previous_context &&
        !SDL_GL_MakeCurrent(previous_window, previous_context))
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Cannot restore the previous OpenGL context: %s", SDL_GetError());
    bongo_cat_memory_policy_model_loaded();
    model_runtime_stage(app, "completed", entry);
    app->loading_model[0] = '\0';
    app->model_load_runtime_stage = 0;
    bongo_cat_runtime_stage(app, "running");
    return true;
}

bool bongo_cat_app_select_model(BongoCatApp *app, const char *id) {
    return bongo_cat_app_select_model_with_error(app, id, NULL);
}
