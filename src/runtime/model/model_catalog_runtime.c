#include "runtime.h"
#include "model_import.h"
#include "model_storage.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ModelSelection {
    char additional[BONGO_CAT_ADDITIONAL_MODEL_CAP][BONGO_CAT_ID_CAP];
    size_t count;
} ModelSelection;

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

static size_t models_using_storage(const BongoCatModelCatalog *models,
    const char *directory) {
    size_t count = 0;
    if (!models || !directory || !directory[0]) return 0;
    for (size_t i = 0; i < models->count; ++i)
        if (!strcmp(models->entries[i].storage_directory, directory)) count++;
    return count;
}

static bool storage_package_id(const char *directory,
    char output[BONGO_CAT_ID_CAP]) {
    const char *name = directory ? bongo_cat_path_name(directory) : NULL;
    return name && name[0] && bongo_cat_import_package_id(output,
        BONGO_CAT_ID_CAP, name);
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
    bongo_cat_models_init(&app->models);
    if (bongo_cat_model_install_builtins(app->asset_root, app->models_root,
        &error) != BONGO_CAT_OK && error.message[0])
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
    if (cleanup) bongo_cat_model_cleanup_imports(app->models_root, &error);
    bongo_cat_models_scan(&app->models, app->models_root, false, &error);
    error = (BongoCatError){0};
    if (bongo_cat_import_installed_models(app, app->models_root, &error) !=
            BONGO_CAT_OK && error.message[0])
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
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
    char package_id[BONGO_CAT_ID_CAP];
    if (!directory[0] || !storage_package_id(directory, package_id)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Model storage directory is invalid: %s", id);
        return BONGO_CAT_ERROR_FORMAT;
    }
    bool shared_storage = models_using_storage(&app->models, directory) > 1;
    bongo_cat_settings_validate(&app->settings);
    bool already_removed = bongo_cat_settings_model_removed(
        &app->settings, id);
    if (shared_storage && !already_removed &&
        app->settings.removed_model_count >= BONGO_CAT_MODEL_CAP) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Too many removed model versions are recorded");
        return BONGO_CAT_ERROR_FORMAT;
    }
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
    if (shared_storage && !already_removed &&
        !bongo_cat_settings_set_model_removed(&app->settings, id, true)) {
        if (primary) {
            BongoCatError restore_error = {0};
            if (!bongo_cat_app_select_model_with_error(app, id,
                    &restore_error))
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Unable to restore model after recording deletion failed: %s",
                    restore_error.message[0] ? restore_error.message :
                    "unknown error");
        }
        if (additional) restore_model_selection(app, &previous_selection);
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Cannot record removed model version: %s", id);
        return BONGO_CAT_ERROR_FORMAT;
    }
    if (!shared_storage && !bongo_cat_model_remove_tree(directory, error)) {
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
    if (!shared_storage)
        bongo_cat_settings_restore_model_package(&app->settings, package_id);
    bongo_cat_app_forget_behavior_state(app, id);
    bongo_cat_settings_set_model_label(&app->settings, id, "");
    bongo_cat_app_rescan_models(app);
    return BONGO_CAT_OK;
}
