#include "runtime.h"
#include "model_import.h"
#include "model_storage.h"
#include "bongo_cat/file.h"
#include "bongo_cat/overlay.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TreeContext {
    const char *source;
    const char *target;
    BongoCatError *error;
    unsigned depth;
} TreeContext;

#define MODEL_TREE_DEPTH_CAP 32

static void select_model_state(BongoCatApp *app, const BongoCatModelEntry *entry) {
    snprintf(app->config.current_model, sizeof(app->config.current_model), "%s",
        entry->id);
    app->config.current_mode = entry->mode;
    bongo_cat_gamepads_set_enabled(app, entry->mode == BONGO_CAT_MODE_GAMEPAD);
}

static void request_model_frame(BongoCatApp *app) {
    if (!app) return;
    if (app->window && app->config.window.visible) {
        if (SDL_GetWindowFlags(app->window) & SDL_WINDOW_HIDDEN)
            bongo_cat_window_set_visible(app, true);
        else bongo_cat_window_clamp_to_display(app);
    }
    bongo_cat_window_mark_hit_dirty(app);
    app->dirty = true;
}

static void commit_model(BongoCatApp *app,
    const BongoCatModelEntry *entry) {
    select_model_state(app, entry);
    app->model_selection_serial++;
    request_model_frame(app);
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
        commit_model(app, entry);
        return true;
    }
    BongoCatError optional = {0};
    BongoCatBehaviorCatalog *behaviors = calloc(1, sizeof(*behaviors));
    if (!behaviors) {
        bongo_cat_error_set(failure, BONGO_CAT_ERROR_MEMORY,
            "Cannot allocate model behavior state");
        return false;
    }
    if (bongo_cat_behaviors_load(behaviors, entry, &optional) != BONGO_CAT_OK)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", optional.message);
    int pixel_width = app->config.window.width, pixel_height = app->config.window.height;
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
    if (bongo_cat_live2d_load(app->live2d, entry->directory,
        entry->setting_file, entry->preset, failure) != BONGO_CAT_OK) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", failure->message);
        if (restore_context && previous_window && previous_context &&
            !SDL_GL_MakeCurrent(previous_window, previous_context))
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "Cannot restore the previous OpenGL context: %s", SDL_GetError());
        free(behaviors);
        if (!bongo_cat_live2d_ready(app->live2d)) app->loaded_model[0] = '\0';
        request_model_frame(app);
        return false;
    }
    app->behaviors = *behaviors;
    free(behaviors);
    optional = (BongoCatError){0};
    if (bongo_cat_overlay_load(app->overlay, entry->adapter_directory,
        &optional) != BONGO_CAT_OK && optional.message[0])
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", optional.message);
    snprintf(app->loaded_model, sizeof(app->loaded_model), "%s", entry->id);
    app->pointer_known = false;
    bongo_cat_live2d_resize(app->live2d, pixel_width, pixel_height);
    if (restore_context && previous_window && previous_context &&
        !SDL_GL_MakeCurrent(previous_window, previous_context))
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Cannot restore the previous OpenGL context: %s", SDL_GetError());
    commit_model(app, entry);
    return true;
}

bool bongo_cat_app_select_model(BongoCatApp *app, const char *id) {
    return bongo_cat_app_select_model_with_error(app, id, NULL);
}

static bool copy_tree(const char *source, const char *target, unsigned depth,
    BongoCatError *error);
static BongoCatPathVisit copy_item(void *userdata,
    const char *dirname, const char *name) {
    (void)dirname;
    TreeContext *context = userdata;
    char source[BONGO_CAT_PATH_CAP], target[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(source, sizeof(source), context->source, name) ||
        !bongo_cat_path_join(target, sizeof(target), context->target, name)) {
        bongo_cat_error_set(context->error, BONGO_CAT_ERROR_IO, "Model path is too long");
        return BONGO_CAT_PATH_FAILURE;
    }
    bool directory = bongo_cat_path_is_dir(source);
    bool file = !directory && bongo_cat_path_is_file(source);
    bool ok = directory
        ? copy_tree(source, target, context->depth + 1, context->error)
        : file && bongo_cat_path_copy_file(source, target);
    if (!ok && context->error && !context->error->message[0])
        bongo_cat_error_set(context->error, BONGO_CAT_ERROR_IO, "Cannot copy %s: %s",
            source, SDL_GetError());
    return ok ? BONGO_CAT_PATH_CONTINUE : BONGO_CAT_PATH_FAILURE;
}

static bool copy_tree(const char *source, const char *target, unsigned depth,
    BongoCatError *error) {
    if (depth > MODEL_TREE_DEPTH_CAP) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Model directory nesting exceeds %u levels", MODEL_TREE_DEPTH_CAP);
        return false;
    }
    if (!bongo_cat_path_create_directory(target)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO, "Cannot create %s: %s", target, SDL_GetError());
        return false;
    }
    TreeContext context = {source, target, error, depth};
    return bongo_cat_path_enumerate(source, copy_item, &context);
}

BongoCatResult bongo_cat_copy_directory(const char *source, const char *target,
    BongoCatError *error) {
    if (!source || !target || !bongo_cat_path_is_dir(source)) return BONGO_CAT_ERROR_ARGUMENT;
    return copy_tree(source, target, 0, error) ? BONGO_CAT_OK :
        error && error->code == BONGO_CAT_ERROR_FORMAT ?
        BONGO_CAT_ERROR_FORMAT : BONGO_CAT_ERROR_IO;
}

static bool custom_root(BongoCatApp *app, char *path, size_t capacity) {
    return app->data_root[0] &&
        bongo_cat_path_join(path, capacity, app->data_root, "custom-models") &&
        bongo_cat_path_create_directory(path);
}

static bool shortcut_model_exists(const BongoCatModelCatalog *models,
    const char *shortcut_id) {
    const char *separator = shortcut_id ? strchr(shortcut_id, ':') : NULL;
    if (!separator) return false;
    size_t length = (size_t)(separator - shortcut_id);
    for (size_t i = 0; i < models->count; ++i) {
        const char *id = models->entries[i].id;
        if (strlen(id) == length && strncmp(id, shortcut_id, length) == 0) return true;
    }
    return false;
}

static void prune_behavior_shortcuts(BongoCatApp *app) {
    size_t output = 0;
    for (size_t i = 0; i < app->config.behavior_shortcut_count; ++i) {
        BongoCatBehaviorShortcut *value = &app->config.behavior_shortcuts[i];
        if (!shortcut_model_exists(&app->models, value->id)) continue;
        if (output != i) app->config.behavior_shortcuts[output] = *value;
        output++;
    }
    app->config.behavior_shortcut_count = output;
}

static bool same_scan_root(const char *left, const char *right) {
    size_t a = left ? strlen(left) : 0, b = right ? strlen(right) : 0;
    while (a > 1 && (left[a - 1] == '/' || left[a - 1] == '\\')) a--;
    while (b > 1 && (right[b - 1] == '/' || right[b - 1] == '\\')) b--;
    if (a != b) return false;
#ifdef _WIN32
    return SDL_strncasecmp(left, right, a) == 0;
#else
    return strncmp(left, right, a) == 0;
#endif
}

static void scan_portable_root(BongoCatApp *app, const char *root) {
    if (!root || !root[0] || !bongo_cat_path_is_dir(root)) return;
    size_t before = app->models.count;
    BongoCatError error = {0};
    BongoCatResult result = bongo_cat_import_portable_mver_scan(app, root, &error);
    size_t added = app->models.count - before;
    if (added) SDL_Log("Nearby model scan added %llu model modes from %s",
        (unsigned long long)added, root);
    if (result != BONGO_CAT_OK && error.message[0])
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
}

void bongo_cat_app_rescan_models(BongoCatApp *app) {
    if (!app) return;
    BongoCatError error = {0};
    char root[BONGO_CAT_PATH_CAP];
    bongo_cat_models_init(&app->models);
    bongo_cat_path_join(root, sizeof(root), app->asset_root, "models");
    bongo_cat_models_scan(&app->models, root, true, &error);
    if (custom_root(app, root, sizeof(root))) {
        bongo_cat_model_cleanup_imports(root, &error);
        bongo_cat_models_scan(&app->models, root, false, &error);
    }
    const char *base = SDL_GetBasePath();
    scan_portable_root(app, base);
    const char *desktop = SDL_GetUserFolder(SDL_FOLDER_DESKTOP);
    if (desktop && !same_scan_root(desktop, base))
        scan_portable_root(app, desktop);
    prune_behavior_shortcuts(app);
    for (size_t i = 0; i < app->models.count; ++i)
        if (!app->models.entries[i].preset)
            bongo_cat_import_apply_metadata(app, app->models.entries[i].id,
                app->models.entries[i].adapter_directory);
}

BongoCatResult bongo_cat_app_remove_model(BongoCatApp *app, const char *id,
    BongoCatError *error) {
    if (!app || !id) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT, "Missing model id");
        return BONGO_CAT_ERROR_ARGUMENT;
    }
    const BongoCatModelEntry *entry = bongo_cat_models_find(&app->models, id);
    if (!entry) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT, "Model is not installed: %s", id);
        return BONGO_CAT_ERROR_ARGUMENT;
    }
    if (entry->preset || entry->managed) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
            entry->managed ? "Portable Mver models are managed by their source directory: %s" :
            "Built-in models cannot be removed: %s", id);
        return BONGO_CAT_ERROR_ARGUMENT;
    }
    bool selected = strcmp(id, app->config.current_model) == 0;
    char directory[BONGO_CAT_PATH_CAP];
    snprintf(directory, sizeof(directory), "%s", entry->storage_directory);
    if (!bongo_cat_model_remove_tree(directory, error)) return BONGO_CAT_ERROR_IO;
    bongo_cat_app_rescan_models(app);
    if (selected) {
        BongoCatError load_error = {0};
        for (size_t i = 0; i < app->models.count; ++i)
            if (bongo_cat_app_select_model_with_error(app,
                app->models.entries[i].id, &load_error)) return BONGO_CAT_OK;
        bongo_cat_error_set(error, BONGO_CAT_ERROR_CUBISM,
            "Model was removed, but no replacement could be displayed: %s",
            load_error.message[0] ? load_error.message : "no installed models");
        return BONGO_CAT_ERROR_CUBISM;
    }
    return BONGO_CAT_OK;
}
