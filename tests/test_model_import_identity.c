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

int test_model_import_identity(void) {
    failures = 0;
    char *temporary = SDL_GetCurrentDirectory();
    CHECK(temporary != NULL);
    if (!temporary) return failures;
    char root[BONGO_CAT_PATH_CAP], data[BONGO_CAT_PATH_CAP];
    char source[BONGO_CAT_PATH_CAP], config[BONGO_CAT_PATH_CAP];
    char manifest[BONGO_CAT_PATH_CAP];
    unsigned long long nonce = (unsigned long long)SDL_GetTicksNS();
    snprintf(root, sizeof(root), "%s/bongo-cat-import-identity-%llu",
        temporary, nonce);
    snprintf(data, sizeof(data), "%s/bongo-cat-import-data-%llu",
        temporary, nonce);
    CHECK(SDL_CreateDirectory(root));
    CHECK(SDL_CreateDirectory(data));
    CHECK(child(source, sizeof(source), root, "source", false));
    CHECK(mver_fixture(source));
    CHECK(child(config, sizeof(config), source, "config.json", false));
    CHECK(child(manifest, sizeof(manifest), source,
        "img/standard/cat_model/cat.model3.json", false));
    BongoCatError error = {0};
    BongoCatImportDiscovery discovered = {0};
    CHECK(bongo_cat_import_mver_discover(source, &discovered, &error) == 1);
    char digest[65];
    bool placeholder = true;
    CHECK(bongo_cat_import_candidate_inspect(&discovered.candidates[0],
        digest, &placeholder, &error) && !placeholder);
    BongoCatImportReceipt first = {0}, repeated = {0};
    CHECK(bongo_cat_import_install(config, data, &first, &error) ==
        BONGO_CAT_OK);
    CHECK(first.count == 1 && first.installed_count == 1 && first.installed[0]);
    CHECK(bongo_cat_import_install(manifest, data, &repeated, &error) ==
        BONGO_CAT_OK);
    CHECK(repeated.count == 1 && repeated.installed_count == 0 &&
        !repeated.installed[0] && strcmp(first.ids[0], repeated.ids[0]) == 0);
    char models_root[BONGO_CAT_PATH_CAP], adapter_metadata[BONGO_CAT_PATH_CAP];
    CHECK(child(models_root, sizeof(models_root), data, "models", false));
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
