#include "runtime.h"
#include "model_import.h"
#include "model_storage.h"
#include "bongo_cat_neo/file.h"
#include "bongo_cat_neo/overlay.h"
#include "bongo_cat_neo/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TreeContext {
    const char *source;
    const char *target;
    BongoCatNeoError *error;
} TreeContext;

static void select_model_state(BongoCatNeoApp *app, const BongoCatNeoModelEntry *entry) {
    snprintf(app->config.current_model, sizeof(app->config.current_model), "%s",
        entry->id);
    app->config.current_mode = entry->mode;
    bongo_cat_neo_gamepads_set_enabled(app, entry->mode == BONGO_CAT_NEO_MODE_GAMEPAD);
}

bool bongo_cat_neo_app_select_model(BongoCatNeoApp *app, const char *id) {
    if (!app || !app->live2d || !id) return false;
    const BongoCatNeoModelEntry *entry = bongo_cat_neo_models_find(&app->models, id);
    if (!entry) return false;
    if (app->loaded_model[0] && strcmp(app->loaded_model, entry->id) == 0) {
        select_model_state(app, entry);
        return true;
    }
    BongoCatNeoError error = {0};
    BongoCatNeoBehaviorCatalog *behaviors = calloc(1, sizeof(*behaviors));
    if (!behaviors) return false;
    if (bongo_cat_neo_behaviors_load(behaviors, entry, &error) != BONGO_CAT_NEO_OK)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
    SDL_Window *previous_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
    bool restore_context = previous_window != app->window ||
        previous_context != app->gl_context;
    if (restore_context && !SDL_GL_MakeCurrent(app->window, app->gl_context)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Cannot activate the main OpenGL context: %s", SDL_GetError());
        free(behaviors);
        return false;
    }
    if (bongo_cat_neo_live2d_load(app->live2d, entry->directory,
        entry->setting_file, &error) != BONGO_CAT_NEO_OK) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
        if (restore_context) SDL_GL_MakeCurrent(previous_window, previous_context);
        free(behaviors);
        return false;
    }
    app->behaviors = *behaviors;
    free(behaviors);
    bongo_cat_neo_overlay_load(app->overlay, entry->directory, &error);
    snprintf(app->loaded_model, sizeof(app->loaded_model), "%s", entry->id);
    select_model_state(app, entry);
    int pixel_width = app->config.window.width, pixel_height = app->config.window.height;
    if (app->window) SDL_GetWindowSizeInPixels(app->window, &pixel_width, &pixel_height);
    bongo_cat_neo_live2d_resize(app->live2d, pixel_width, pixel_height);
    if (restore_context) SDL_GL_MakeCurrent(previous_window, previous_context);
    bongo_cat_neo_window_mark_hit_dirty(app);
    app->dirty = true;
    return true;
}

static bool copy_tree(const char *source, const char *target, BongoCatNeoError *error);
static SDL_EnumerationResult SDLCALL copy_item(void *userdata,
    const char *dirname, const char *name) {
    (void)dirname;
    TreeContext *context = userdata;
    char source[BONGO_CAT_NEO_PATH_CAP], target[BONGO_CAT_NEO_PATH_CAP];
    if (!bongo_cat_neo_path_join(source, sizeof(source), context->source, name) ||
        !bongo_cat_neo_path_join(target, sizeof(target), context->target, name)) {
        bongo_cat_neo_error_set(context->error, BONGO_CAT_NEO_ERROR_IO, "Model path is too long");
        return SDL_ENUM_FAILURE;
    }
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(source, &info)) {
        bongo_cat_neo_error_set(context->error, BONGO_CAT_NEO_ERROR_IO, "%s", SDL_GetError());
        return SDL_ENUM_FAILURE;
    }
    bool ok = info.type == SDL_PATHTYPE_DIRECTORY
        ? copy_tree(source, target, context->error)
        : info.type == SDL_PATHTYPE_FILE && SDL_CopyFile(source, target);
    if (!ok && context->error && !context->error->message[0])
        bongo_cat_neo_error_set(context->error, BONGO_CAT_NEO_ERROR_IO, "Cannot copy %s: %s",
            source, SDL_GetError());
    return ok ? SDL_ENUM_CONTINUE : SDL_ENUM_FAILURE;
}

static bool copy_tree(const char *source, const char *target, BongoCatNeoError *error) {
    if (!SDL_CreateDirectory(target)) {
        bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_IO, "Cannot create %s: %s", target, SDL_GetError());
        return false;
    }
    TreeContext context = {source, target, error};
    return SDL_EnumerateDirectory(source, copy_item, &context);
}

BongoCatNeoResult bongo_cat_neo_copy_directory(const char *source, const char *target,
    BongoCatNeoError *error) {
    if (!source || !target || !bongo_cat_neo_path_is_dir(source)) return BONGO_CAT_NEO_ERROR_ARGUMENT;
    return copy_tree(source, target, error) ? BONGO_CAT_NEO_OK : BONGO_CAT_NEO_ERROR_IO;
}

static bool custom_root(BongoCatNeoApp *app, char *path, size_t capacity) {
    return app->data_root[0] &&
        bongo_cat_neo_path_join(path, capacity, app->data_root, "custom-models") &&
        SDL_CreateDirectory(path);
}

static bool shortcut_model_exists(const BongoCatNeoModelCatalog *models,
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

static void prune_behavior_shortcuts(BongoCatNeoApp *app) {
    size_t output = 0;
    for (size_t i = 0; i < app->config.behavior_shortcut_count; ++i) {
        BongoCatNeoBehaviorShortcut *value = &app->config.behavior_shortcuts[i];
        if (!shortcut_model_exists(&app->models, value->id)) continue;
        if (output != i) app->config.behavior_shortcuts[output] = *value;
        output++;
    }
    app->config.behavior_shortcut_count = output;
}

void bongo_cat_neo_app_rescan_models(BongoCatNeoApp *app) {
    if (!app) return;
    BongoCatNeoError error = {0};
    char root[BONGO_CAT_NEO_PATH_CAP];
    bongo_cat_neo_models_init(&app->models);
    bongo_cat_neo_path_join(root, sizeof(root), app->asset_root, "models");
    bongo_cat_neo_models_scan(&app->models, root, true, &error);
    if (custom_root(app, root, sizeof(root))) {
        bongo_cat_neo_model_cleanup_imports(root, &error);
        bongo_cat_neo_models_scan(&app->models, root, false, &error);
    }
    prune_behavior_shortcuts(app);
    for (size_t i = 0; i < app->models.count; ++i)
        if (!app->models.entries[i].preset)
            bongo_cat_neo_import_apply_metadata(app, app->models.entries[i].id,
                app->models.entries[i].directory);
}

BongoCatNeoResult bongo_cat_neo_app_remove_model(BongoCatNeoApp *app, const char *id,
    BongoCatNeoError *error) {
    if (!app || !id) {
        bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_ARGUMENT, "Missing model id");
        return BONGO_CAT_NEO_ERROR_ARGUMENT;
    }
    const BongoCatNeoModelEntry *entry = bongo_cat_neo_models_find(&app->models, id);
    if (!entry) {
        bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_ARGUMENT, "Model is not installed: %s", id);
        return BONGO_CAT_NEO_ERROR_ARGUMENT;
    }
    if (entry->preset) {
        bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_ARGUMENT, "Built-in models cannot be removed: %s", id);
        return BONGO_CAT_NEO_ERROR_ARGUMENT;
    }
    bool selected = strcmp(id, app->config.current_model) == 0;
    char directory[BONGO_CAT_NEO_PATH_CAP];
    snprintf(directory, sizeof(directory), "%s", entry->directory);
    if (!bongo_cat_neo_model_remove_tree(directory, error)) return BONGO_CAT_NEO_ERROR_IO;
    bongo_cat_neo_app_rescan_models(app);
    if (selected && app->models.count) bongo_cat_neo_app_select_model(app, app->models.entries[0].id);
    return BONGO_CAT_NEO_OK;
}
