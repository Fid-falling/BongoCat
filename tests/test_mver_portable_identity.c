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

int test_mver_portable_identity(void) {
    failures = 0;
    char *temporary = SDL_GetCurrentDirectory();
    CHECK(temporary != NULL);
    if (!temporary) return failures;
    char root[BONGO_CAT_PATH_CAP], data[BONGO_CAT_PATH_CAP];
    char source[BONGO_CAT_PATH_CAP], moved[BONGO_CAT_PATH_CAP];
    char again[BONGO_CAT_PATH_CAP], duplicate[BONGO_CAT_PATH_CAP];
    unsigned long long nonce = (unsigned long long)SDL_GetTicksNS();
    snprintf(root, sizeof(root), "%s/bongo-cat-portable-move-%llu",
        temporary, nonce);
    snprintf(data, sizeof(data), "%s/bongo-cat-portable-data-%llu",
        temporary, nonce);
    CHECK(SDL_CreateDirectory(root));
    CHECK(SDL_CreateDirectory(data));
    CHECK(child(source, sizeof(source), root, "source", false));
    CHECK(child(moved, sizeof(moved), root, "moved", false));
    CHECK(child(again, sizeof(again), root, "again", false));
    CHECK(child(duplicate, sizeof(duplicate), root, "duplicate", false));
    CHECK(portable_fixture(source));
    BongoCatApp *app = calloc(1, sizeof(*app));
    CHECK(app != NULL);
    if (!app) goto cleanup;
    bongo_cat_config_defaults(&app->config);
    bongo_cat_models_init(&app->models);
    snprintf(app->data_root, sizeof(app->data_root), "%s", data);
    BongoCatError error = {0};
    BongoCatImportReceipt first_install = {0}, repeated_install = {0};
    CHECK(bongo_cat_import_install(source, data, &first_install, &error) ==
        BONGO_CAT_OK);
    CHECK(first_install.count == 1 && first_install.installed_count == 1 &&
        first_install.installed[0]);
    CHECK(bongo_cat_import_install(source, data, &repeated_install, &error) ==
        BONGO_CAT_OK);
    CHECK(repeated_install.count == 1 && repeated_install.installed_count == 0 &&
        !repeated_install.installed[0] &&
        strcmp(first_install.ids[0], repeated_install.ids[0]) == 0);
    char custom_root[BONGO_CAT_PATH_CAP], adapter_metadata[BONGO_CAT_PATH_CAP];
    CHECK(child(custom_root, sizeof(custom_root), data, "custom-models", false));
    BongoCatModelCatalog *installed = calloc(1, sizeof(*installed));
    CHECK(installed != NULL);
    if (installed) {
        CHECK(bongo_cat_models_scan(installed, custom_root, false, &error) ==
            BONGO_CAT_OK);
        CHECK(installed->count == 1 && installed->entries[0].package_schema == 2 &&
            installed->entries[0].source_format == BONGO_CAT_MODEL_SOURCE_MVER &&
            installed->entries[0].content_digest[0] != '\0' &&
            (installed->entries[0].capabilities &
                BONGO_CAT_MODEL_CAPABILITY_MVER_PROJECTION));
        CHECK(child(adapter_metadata, sizeof(adapter_metadata),
            installed->entries[0].adapter_directory,
            ".bongo-cat-adapter.json", false));
        CHECK(bongo_cat_path_is_file(adapter_metadata));
        free(installed);
    }
    CHECK(bongo_cat_import_portable_mver(app, source, &error) == BONGO_CAT_OK);
    CHECK(app->models.count == 1);
    char old_id[BONGO_CAT_ID_CAP];
    snprintf(old_id, sizeof(old_id), "%s", app->models.entries[0].id);
    CHECK(bongo_cat_config_set_model_label(&app->config, old_id,
        "Portable custom name"));
    BongoCatBehaviorShortcut *binding =
        &app->config.behavior_shortcuts[app->config.behavior_shortcut_count++];
    snprintf(binding->id, sizeof(binding->id), "%s:expression:2", old_id);
    snprintf(binding->shortcut, sizeof(binding->shortcut), "Alt+3");
    snprintf(binding->label, sizeof(binding->label), "Custom expression");
    CHECK(bongo_cat_path_rename(source, moved));
    bongo_cat_models_init(&app->models);
    CHECK(bongo_cat_import_portable_mver(app, moved, &error) == BONGO_CAT_OK);
    CHECK(app->models.count == 1);
    const char *new_id = app->models.entries[0].id;
    CHECK(strcmp(old_id, new_id) != 0);
    CHECK(strcmp(app->config.current_model, new_id) == 0);
    CHECK(strcmp(bongo_cat_config_model_label(&app->config, new_id),
        "Portable custom name") == 0);
    CHECK(bongo_cat_config_model_label(&app->config, old_id) == NULL);
    CHECK(app->config.behavior_shortcut_count == 1);
    CHECK(strncmp(app->config.behavior_shortcuts[0].id, new_id,
        strlen(new_id)) == 0);
    CHECK(strcmp(app->config.behavior_shortcuts[0].shortcut, "Alt+3") == 0);
    CHECK(strcmp(app->config.behavior_shortcuts[0].label,
        "Custom expression") == 0);
    char ambiguous_id[BONGO_CAT_ID_CAP];
    snprintf(ambiguous_id, sizeof(ambiguous_id), "%s", new_id);
    CHECK(bongo_cat_config_set_model_label(&app->config, ambiguous_id,
        "Must remain here"));
    CHECK(bongo_cat_path_rename(moved, again));
    CHECK(portable_fixture(duplicate));
    bongo_cat_models_init(&app->models);
    CHECK(bongo_cat_import_portable_mver(app, root, &error) == BONGO_CAT_OK);
    CHECK(app->models.count == 2);
    CHECK(strcmp(app->config.current_model, ambiguous_id) == 0);
    CHECK(strcmp(bongo_cat_config_model_label(&app->config, ambiguous_id),
        "Must remain here") == 0);
    free(app);
cleanup:
    CHECK(bongo_cat_model_remove_tree(root, NULL));
    CHECK(bongo_cat_model_remove_tree(data, NULL));
    SDL_free(temporary);
    return failures;
}
