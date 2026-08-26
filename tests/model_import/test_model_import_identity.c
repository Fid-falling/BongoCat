#include "test_mver_support.h"

#include "model_import.h"
#include "model_storage.h"
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

static void disabled_mver_live2d_is_not_imported(const char *temporary) {
    char root[BONGO_CAT_PATH_CAP], config[BONGO_CAT_PATH_CAP];
    snprintf(root, sizeof(root), "%s/bongo-cat-disabled-mver-%llu", temporary,
        (unsigned long long)SDL_GetTicksNS());
    CHECK(mver_fixture(root));
    CHECK(child(config, sizeof(config), root, "config.json", false));
    CHECK(write_text(config, "{\"standard\":{\"l2d\":false,"
        "\"keyboard\":[[65]],\"hand\":[[65]]}}"));
    BongoCatImportDiscovery discovery = {0};
    BongoCatError error = {0};
    CHECK(bongo_cat_import_mver_discover_exact(root, &discovery, &error) < 0);
    CHECK(discovery.count == 0);
    CHECK(strstr(error.message, "no supported model modes") != NULL);
    CHECK(bongo_cat_model_remove_tree(root, NULL));
}

int test_model_import_identity(void) {
    failures = 0;
    char *temporary = SDL_GetCurrentDirectory();
    CHECK(temporary != NULL);
    if (!temporary) return failures;
    disabled_mver_live2d_is_not_imported(temporary);
    char root[BONGO_CAT_PATH_CAP], data[BONGO_CAT_PATH_CAP];
    char source[BONGO_CAT_PATH_CAP], config[BONGO_CAT_PATH_CAP];
    char manifest[BONGO_CAT_PATH_CAP], missing[BONGO_CAT_PATH_CAP];
    unsigned long long nonce = (unsigned long long)SDL_GetTicksNS();
    snprintf(root, sizeof(root), "%s/bongo-cat-import-identity-%llu",
        temporary, nonce);
    snprintf(data, sizeof(data), "%s/bongo-cat-import-data-%llu",
        temporary, nonce);
    CHECK(SDL_CreateDirectory(root));
    CHECK(SDL_CreateDirectory(data));
    char models_root[BONGO_CAT_PATH_CAP];
    CHECK(child(models_root, sizeof(models_root), data, "models", true));
    CHECK(child(source, sizeof(source), root, "source", false));
    CHECK(mver_fixture(source));
    CHECK(child(config, sizeof(config), source, "config.json", false));
    CHECK(child(manifest, sizeof(manifest), source,
        "img/standard/cat_model/cat.model3.json", false));
    CHECK(child(missing, sizeof(missing), root, "missing", false));
    BongoCatError error = {0};
    BongoCatImportDiscovery discovered = {0};
    CHECK(bongo_cat_import_mver_discover(source, &discovered, &error) == 1);
    char digest[65];
    bool placeholder = true;
    CHECK(bongo_cat_import_candidate_inspect(&discovered.candidates[0],
        digest, &placeholder, &error) && !placeholder);
    BongoCatImportReceipt first = {0}, repeated = {0}, wrapped = {0};
    BongoCatImportSession *session = bongo_cat_import_session_create(models_root,
        &error);
    CHECK(session != NULL);
    CHECK(session && bongo_cat_import_session_install(session, config, &first,
        &error) == BONGO_CAT_OK);
    CHECK(first.count == 1 && first.installed_count == 1 && first.installed[0]);
    CHECK(session && bongo_cat_import_session_install(session, missing, NULL,
        &error) != BONGO_CAT_OK);
    error = (BongoCatError){0};
    CHECK(session && bongo_cat_import_session_install(session, manifest,
        &repeated, &error) == BONGO_CAT_OK);
    CHECK(repeated.count == 1 && repeated.installed_count == 0 &&
        !repeated.installed[0] && strcmp(first.ids[0], repeated.ids[0]) == 0);
    bongo_cat_import_session_destroy(session);
    CHECK(bongo_cat_import_install(manifest, models_root, &wrapped, &error) ==
        BONGO_CAT_OK);
    CHECK(wrapped.count == 1 && wrapped.installed_count == 0 &&
        strcmp(first.ids[0], wrapped.ids[0]) == 0);
    char adapter_metadata[BONGO_CAT_PATH_CAP];
    BongoCatModelCatalog *installed = calloc(1, sizeof(*installed));
    CHECK(installed != NULL);
    if (installed) {
        CHECK(bongo_cat_models_scan(installed, models_root, false, &error) ==
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
    CHECK(bongo_cat_model_remove_tree(root, NULL));
    CHECK(bongo_cat_model_remove_tree(data, NULL));
    SDL_free(temporary);
    return failures;
}
