#include "test_mver_support.h"

#include "model_import.h"
#include "model_storage.h"
#include "runtime.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #value); \
    failures++; \
} } while (0)

int test_mver_nearby_identity(void) {
    failures = 0;
    char *temporary = SDL_GetCurrentDirectory();
    CHECK(temporary != NULL);
    if (!temporary) return failures;
    char root[BONGO_CAT_PATH_CAP], data[BONGO_CAT_PATH_CAP];
    char source[BONGO_CAT_PATH_CAP], moved[BONGO_CAT_PATH_CAP];
    char again[BONGO_CAT_PATH_CAP], duplicate[BONGO_CAT_PATH_CAP];
    unsigned long long nonce = (unsigned long long)SDL_GetTicksNS();
    snprintf(root, sizeof(root), "%s/bongo-cat-nearby-move-%llu",
        temporary, nonce);
    snprintf(data, sizeof(data), "%s/bongo-cat-nearby-data-%llu",
        temporary, nonce);
    CHECK(SDL_CreateDirectory(root));
    CHECK(SDL_CreateDirectory(data));
    CHECK(child(source, sizeof(source), root, "source", false));
    CHECK(child(moved, sizeof(moved), root, "moved", false));
    CHECK(child(again, sizeof(again), root, "again", false));
    CHECK(child(duplicate, sizeof(duplicate), root, "duplicate", false));
    CHECK(mver_fixture(source));
    BongoCatApp *app = calloc(1, sizeof(*app));
    CHECK(app != NULL);
    if (!app) goto cleanup;
    bongo_cat_settings_defaults(&app->settings);
    bongo_cat_session_defaults(&app->session);
    bongo_cat_models_init(&app->models);
    snprintf(app->data_root, sizeof(app->data_root), "%s", data);
    snprintf(app->cache_root, sizeof(app->cache_root), "%s", data);
    BongoCatError error = {0};
    CHECK(bongo_cat_import_nearby_root(app, source, &error) ==
        BONGO_CAT_OK);
    CHECK(app->models.count == 1 && app->models.entries[0].managed);
    char old_id[BONGO_CAT_ID_CAP];
    snprintf(old_id, sizeof(old_id), "%s", app->models.entries[0].id);
    CHECK(bongo_cat_settings_set_model_label(&app->settings, old_id,
        "Nearby custom name"));
    BongoCatBehaviorShortcut *binding =
        &app->settings.behavior_shortcuts[app->settings.behavior_shortcut_count++];
    snprintf(binding->id, sizeof(binding->id), "%s:expression:2", old_id);
    snprintf(binding->shortcut, sizeof(binding->shortcut), "Alt+3");
    snprintf(binding->label, sizeof(binding->label), "Custom expression");
    CHECK(bongo_cat_path_rename(source, moved));
    bongo_cat_models_init(&app->models);
    CHECK(bongo_cat_import_nearby_root(app, moved, &error) ==
        BONGO_CAT_OK);
    CHECK(app->models.count == 1 && app->models.entries[0].managed);
    const char *new_id = app->models.entries[0].id;
    CHECK(strcmp(old_id, new_id) != 0);
    CHECK(strcmp(app->session.active_model_id, old_id) == 0);
    CHECK(bongo_cat_settings_model_label(&app->settings, new_id) == NULL);
    CHECK(strcmp(bongo_cat_settings_model_label(&app->settings, old_id),
        "Nearby custom name") == 0);
    CHECK(app->settings.behavior_shortcut_count == 1);
    CHECK(strncmp(app->settings.behavior_shortcuts[0].id, old_id,
        strlen(old_id)) == 0);
    CHECK(strcmp(app->settings.behavior_shortcuts[0].shortcut, "Alt+3") == 0);
    char ambiguous_id[BONGO_CAT_ID_CAP];
    snprintf(ambiguous_id, sizeof(ambiguous_id), "%s", new_id);
    CHECK(bongo_cat_settings_set_model_label(&app->settings, ambiguous_id,
        "Must remain here"));
    CHECK(bongo_cat_path_rename(moved, again));
    CHECK(mver_fixture(duplicate));
    bongo_cat_models_init(&app->models);
    CHECK(bongo_cat_import_nearby_root(app, root, &error) ==
        BONGO_CAT_OK);
    CHECK(app->models.count == 1 && app->models.entries[0].managed);
    CHECK(strcmp(app->session.active_model_id, old_id) == 0);
    CHECK(strcmp(bongo_cat_settings_model_label(&app->settings, ambiguous_id),
        "Must remain here") == 0);
    free(app);
cleanup:
    CHECK(bongo_cat_model_remove_tree(root, NULL));
    CHECK(bongo_cat_model_remove_tree(data, NULL));
    SDL_free(temporary);
    return failures;
}

int test_mver_nearby_refresh(void) {
    failures = 0;
    const char *base = SDL_GetBasePath();
    char *temporary = SDL_GetCurrentDirectory();
    CHECK(base != NULL && temporary != NULL);
    if (!base || !temporary) {
        SDL_free(temporary);
        return failures;
    }

    unsigned long long nonce = (unsigned long long)SDL_GetTicksNS();
    char source[BONGO_CAT_PATH_CAP], data[BONGO_CAT_PATH_CAP];
    snprintf(source, sizeof(source), "%s/bongo-cat-nearby-refresh-%llu",
        base, nonce);
    snprintf(data, sizeof(data), "%s/bongo-cat-nearby-refresh-data-%llu",
        temporary, nonce);
    CHECK(SDL_CreateDirectory(data));

    BongoCatApp *app = calloc(1, sizeof(*app));
    CHECK(app != NULL);
    if (!app) goto cleanup;
    bongo_cat_settings_defaults(&app->settings);
    bongo_cat_session_defaults(&app->session);
    bongo_cat_models_init(&app->models);
    snprintf(app->asset_root, sizeof(app->asset_root),
        "%s/resources/assets", BONGO_CAT_NATIVE_SOURCE_DIR);
    snprintf(app->data_root, sizeof(app->data_root), "%s", data);
    snprintf(app->cache_root, sizeof(app->cache_root), "%s", data);

    bongo_cat_app_refresh_nearby_models(app);
    size_t before = app->models.count;
    CHECK(mver_fixture(source));
    bongo_cat_app_request_nearby_model_refresh(app);
    /* A synchronous catalog mutation invalidates the in-flight snapshot. The
       refresh worker must discard it, rerun, and commit only the newer scan. */
    bongo_cat_app_refresh_installed_models(app);
    uint64_t deadline = SDL_GetTicksNS() + 5000000000ull;
    while (app->models.count == before && SDL_GetTicksNS() < deadline) {
        bongo_cat_model_refresh_update(app);
        SDL_Delay(2);
    }
    CHECK(app->models.count == before + 1);
    CHECK(app->models.entries[app->models.count - 1].managed);
    CHECK(strcmp(app->models.entries[app->models.count - 1].display_name,
        bongo_cat_path_name(source)) == 0);
    CHECK(strcmp(app->session.active_model_id, "standard") == 0);
    bongo_cat_model_refresh_shutdown(app);
    free(app);

cleanup:
    CHECK(bongo_cat_model_remove_tree(source, NULL));
    CHECK(bongo_cat_model_remove_tree(data, NULL));
    SDL_free(temporary);
    return failures;
}
