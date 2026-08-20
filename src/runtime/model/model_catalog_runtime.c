#include "runtime.h"
#include "model_import.h"
#include "model_storage.h"
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

typedef struct ModelSelection {
    char additional[BONGO_CAT_ADDITIONAL_MODEL_CAP][BONGO_CAT_ID_CAP];
    size_t count;
} ModelSelection;

enum { MODEL_TREE_DEPTH_CAP = 32 };

static ModelSelection capture_model_selection(const BongoCatApp *app) {
    ModelSelection selection = {0};
    selection.count = app->session.additional_model_count;
    if (selection.count > BONGO_CAT_ADDITIONAL_MODEL_CAP)
        selection.count = BONGO_CAT_ADDITIONAL_MODEL_CAP;
    memcpy(selection.additional, app->session.additional_model_ids,
        selection.count * sizeof(selection.additional[0]));
    return selection;
}

static void restore_model_selection(BongoCatApp *app,
    const ModelSelection *selection) {
    bongo_cat_session_clear_additional_models(&app->session);
    for (size_t i = 0; i < selection->count; ++i)
        if (bongo_cat_models_find(&app->models, selection->additional[i]))
            bongo_cat_session_add_model(&app->session,
                selection->additional[i]);
    bongo_cat_multi_pet_primary_update(app, SDL_GetTicksNS());
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
        bongo_cat_error_set(context->error, BONGO_CAT_ERROR_IO,
            "Model path is too long");
        return BONGO_CAT_PATH_FAILURE;
    }
    bool directory = bongo_cat_path_is_dir(source);
    bool file = !directory && bongo_cat_path_is_file(source);
    bool ok = directory ? copy_tree(source, target, context->depth + 1,
        context->error) : file && bongo_cat_path_copy_file(source, target);
    if (!ok && context->error && !context->error->message[0])
        bongo_cat_error_set(context->error, BONGO_CAT_ERROR_IO,
            "Cannot copy %s: %s", source, SDL_GetError());
    return ok ? BONGO_CAT_PATH_CONTINUE : BONGO_CAT_PATH_FAILURE;
}

static bool copy_tree(const char *source, const char *target, unsigned depth,
    BongoCatError *error) {
    if (depth > MODEL_TREE_DEPTH_CAP) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Model directory nesting exceeds %u levels",
            MODEL_TREE_DEPTH_CAP);
        return false;
    }
    if (!bongo_cat_path_create_directory(target)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
            "Cannot create %s: %s", target, SDL_GetError());
        return false;
    }
    TreeContext context = {source, target, error, depth};
    return bongo_cat_path_enumerate(source, copy_item, &context);
}

BongoCatResult bongo_cat_copy_directory(const char *source,
    const char *target, BongoCatError *error) {
    if (!source || !target || !bongo_cat_path_is_dir(source))
        return BONGO_CAT_ERROR_ARGUMENT;
    return copy_tree(source, target, 0, error) ? BONGO_CAT_OK :
        error && error->code == BONGO_CAT_ERROR_FORMAT ?
        BONGO_CAT_ERROR_FORMAT : BONGO_CAT_ERROR_IO;
}

static bool custom_root(BongoCatApp *app, char *path, size_t capacity) {
    return app->data_root[0] && bongo_cat_path_join(path, capacity,
        app->data_root, "models") &&
        bongo_cat_path_create_directory(path);
}

static bool shortcut_model_exists(const BongoCatModelCatalog *models,
    const char *shortcut_id) {
    const char *separator = shortcut_id ? strchr(shortcut_id, ':') : NULL;
    if (!separator) return false;
    size_t length = (size_t)(separator - shortcut_id);
    for (size_t i = 0; i < models->count; ++i) {
        const char *id = models->entries[i].id;
        if (strlen(id) == length && !strncmp(id, shortcut_id, length))
            return true;
    }
    return false;
}

static void prune_behavior_shortcuts(BongoCatApp *app) {
    size_t output = 0;
    for (size_t i = 0; i < app->settings.behavior_shortcut_count; ++i) {
        BongoCatBehaviorShortcut *value = &app->settings.behavior_shortcuts[i];
        if (!shortcut_model_exists(&app->models, value->id)) continue;
        if (output != i) app->settings.behavior_shortcuts[output] = *value;
        output++;
    }
    app->settings.behavior_shortcut_count = output;
}

static void scan_nearby_root(BongoCatApp *app, const char *root) {
    if (!root || !root[0] || !bongo_cat_path_is_dir(root)) return;
    size_t before = app->models.count;
    BongoCatError error = {0};
    BongoCatResult result = bongo_cat_import_nearby_scan(app, root,
        &error);
    size_t added = app->models.count - before;
    if (added) SDL_Log("Nearby model scan added %llu models from %s",
        (unsigned long long)added, root);
    if (result != BONGO_CAT_OK && error.message[0])
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
}

static void scan_owned_models(BongoCatApp *app, bool cleanup) {
    BongoCatError error = {0};
    char root[BONGO_CAT_PATH_CAP];
    bongo_cat_models_init(&app->models);
    bongo_cat_path_join(root, sizeof(root), app->asset_root, "models");
    bongo_cat_models_scan(&app->models, root, true, &error);
    if (custom_root(app, root, sizeof(root))) {
        if (cleanup) bongo_cat_model_cleanup_imports(root, &error);
        bongo_cat_models_scan(&app->models, root, false, &error);
    }
}

void bongo_cat_model_catalog_finish(BongoCatApp *app) {
    prune_behavior_shortcuts(app);
    for (size_t i = 0; i < app->models.count; ++i)
        bongo_cat_import_apply_metadata(app, app->models.entries[i].id,
            app->models.entries[i].adapter_directory);
}

void bongo_cat_model_catalog_scan(BongoCatApp *app, bool cleanup,
    const char *nearby_root) {
    if (!app) return;
    scan_owned_models(app, cleanup);
    if (nearby_root && !SDL_getenv("BONGO_CAT_DISABLE_NEARBY_MODEL_SCAN"))
        scan_nearby_root(app, nearby_root);
}

void bongo_cat_app_rescan_models(BongoCatApp *app) {
    if (!app) return;
    bongo_cat_model_refresh_invalidate(app);
    bongo_cat_model_catalog_scan(app, true, app->nearby_root);
    bongo_cat_model_catalog_finish(app);
}

void bongo_cat_app_refresh_installed_models(BongoCatApp *app) {
    if (!app) return;
    bongo_cat_model_refresh_invalidate(app);
    BongoCatModelCatalog *previous = malloc(sizeof(*previous));
    if (!previous) {
        bongo_cat_app_rescan_models(app);
        return;
    }
    *previous = app->models;
    scan_owned_models(app, false);
    for (size_t i = 0; i < previous->count &&
        app->models.count < BONGO_CAT_MODEL_CAP; ++i) {
        const BongoCatModelEntry *entry = &previous->entries[i];
        if (!entry->managed || bongo_cat_models_find(&app->models, entry->id))
            continue;
        app->models.entries[app->models.count++] = *entry;
    }
    free(previous);
    bongo_cat_model_catalog_finish(app);
}

void bongo_cat_app_refresh_nearby_models(BongoCatApp *app) {
    if (!app || SDL_getenv("BONGO_CAT_DISABLE_NEARBY_MODEL_SCAN")) return;
    bongo_cat_model_refresh_invalidate(app);
    char active_model_id[BONGO_CAT_ID_CAP];
    snprintf(active_model_id, sizeof(active_model_id), "%s",
        app->session.active_model_id);
    scan_owned_models(app, false);
    scan_nearby_root(app, app->nearby_root);
    snprintf(app->session.active_model_id,
        sizeof(app->session.active_model_id), "%s", active_model_id);
    bongo_cat_model_catalog_finish(app);
}

BongoCatResult bongo_cat_app_remove_model(BongoCatApp *app, const char *id,
    BongoCatError *error) {
    if (!app || !id) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
            "Missing model id");
        return BONGO_CAT_ERROR_ARGUMENT;
    }
    const BongoCatModelEntry *entry = bongo_cat_models_find(&app->models, id);
    if (!entry) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
            "Model is not installed: %s", id);
        return BONGO_CAT_ERROR_ARGUMENT;
    }
    if (entry->preset || entry->managed) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT, entry->managed
            ? "Nearby models are managed by their source directory: %s"
            : "Built-in models cannot be removed: %s", id);
        return BONGO_CAT_ERROR_ARGUMENT;
    }
    bool primary = !strcmp(id, app->session.active_model_id) ||
        !strcmp(id, app->loaded_model);
    bool additional = !primary && bongo_cat_app_model_active(app, id);
    ModelSelection previous_selection = capture_model_selection(app);
    char directory[BONGO_CAT_PATH_CAP];
    snprintf(directory, sizeof(directory), "%s", entry->storage_directory);
    if (additional) {
        BongoCatError deactivate_error = {0};
        if (!bongo_cat_app_set_model_active(app, id, false,
                &deactivate_error)) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
                "Cannot stop the active model: %s",
                deactivate_error.message);
            return BONGO_CAT_ERROR_PLATFORM;
        }
    } else if (primary) {
        BongoCatError load_error = {0};
        bool replacement = false;
        for (size_t i = 0; i < app->models.count; ++i)
            if (strcmp(app->models.entries[i].id, id) &&
                bongo_cat_app_select_model_with_error(app,
                    app->models.entries[i].id, &load_error)) {
                replacement = true;
                break;
            }
        if (!replacement) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_CUBISM,
                "Cannot delete the active model because no replacement could be displayed: %s",
                load_error.message[0] ? load_error.message :
                "no installed models");
            return BONGO_CAT_ERROR_CUBISM;
        }
    }
    if (!bongo_cat_model_remove_tree(directory, error)) {
        bongo_cat_app_rescan_models(app);
        bool restore_selection = additional;
        if (primary && bongo_cat_models_find(&app->models, id)) {
            BongoCatError restore_error = {0};
            restore_selection = bongo_cat_app_select_model_with_error(app, id,
                &restore_error);
            if (!restore_selection)
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Unable to restore model after deletion failed: %s",
                    restore_error.message[0] ? restore_error.message :
                    "unknown error");
        }
        if (restore_selection)
            restore_model_selection(app, &previous_selection);
        return BONGO_CAT_ERROR_IO;
    }
    bongo_cat_settings_set_model_label(&app->settings, id, "");
    bongo_cat_app_rescan_models(app);
    return BONGO_CAT_OK;
}
