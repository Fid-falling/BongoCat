#include "test_mver_import_internal.h"
#include "test_mver_support.h"
#include "model_import_mver_internal.h"
#include "model_storage.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

void test_mver_container_discovery(void) {
    char root[BONGO_CAT_PATH_CAP], package[BONGO_CAT_PATH_CAP];
    char mode[BONGO_CAT_PATH_CAP];
    char bundle[BONGO_CAT_PATH_CAP], nested_package[BONGO_CAT_PATH_CAP];
    char variant[BONGO_CAT_PATH_CAP], variant_model[BONGO_CAT_PATH_CAP];
    char variant_img[BONGO_CAT_PATH_CAP], variant_standard[BONGO_CAT_PATH_CAP];
    char variant_hand[BONGO_CAT_PATH_CAP], source_hand[BONGO_CAT_PATH_CAP];
    char *temporary = SDL_GetCurrentDirectory();
    CHECK(temporary != NULL);
    if (!temporary) return;
    unsigned long long stamp = (unsigned long long)SDL_GetTicksNS();
    snprintf(root, sizeof(root), "%s/bongo-cat-container-%llu", temporary,
        stamp);
    bongo_cat_model_remove_tree(root, NULL);
    CHECK(SDL_CreateDirectory(root));
    CHECK(child(package, sizeof(package), root, "app", true));
    CHECK(mver_fixture(package));
    CHECK(child(bundle, sizeof(bundle), root, "bundle", true));
    CHECK(child(nested_package, sizeof(nested_package), bundle, "model", true));
    CHECK(mver_fixture(nested_package));
    char nested_config[BONGO_CAT_PATH_CAP], slim_config[BONGO_CAT_PATH_CAP];
    CHECK(child(nested_config, sizeof(nested_config), nested_package,
        "config.json", false));
    CHECK(child(slim_config, sizeof(slim_config), nested_package,
        BONGO_CAT_SKIN_CONFIG_FILE, false));
    CHECK(SDL_RenamePath(nested_config, slim_config));
    CHECK(child(variant, sizeof(variant), bundle, "variant", true));
    CHECK(child(variant_model, sizeof(variant_model), variant, "model", true));
    CHECK(child(variant_img, sizeof(variant_img), variant_model, "img", true));
    CHECK(child(variant_standard, sizeof(variant_standard), variant_img,
        "standard", true));
    CHECK(child(variant_hand, sizeof(variant_hand), variant_standard,
        "hand", true));
    CHECK(child(source_hand, sizeof(source_hand), nested_package,
        "img/standard/hand/0.png", false));
    CHECK(child(mode, sizeof(mode), variant_hand, "0.png", false) &&
        SDL_CopyFile(source_hand, mode));
    char normalized[BONGO_CAT_PATH_CAP];
    BongoCatError source_error = {0};
    CHECK(bongo_cat_import_source_directory(mode, normalized,
        sizeof(normalized), &source_error) == BONGO_CAT_OK);
    CHECK(strcmp(normalized, variant_model) == 0);
    BongoCatImportDiscovery discovery = {0};
    BongoCatError error = {0};
    CHECK(bongo_cat_import_mver_discover_exact(package, &discovery, &error) == 1);
    CHECK(discovery.count == 1);
    CHECK(child(mode, sizeof(mode), package, "img/standard", false));
    BongoCatImportDiscovery nested = {0};
    CHECK(bongo_cat_import_mver_discover_exact(mode, &nested, &error) == 0);
    CHECK(bongo_cat_import_mver_discover(mode, &nested, &error) == 1);
    BongoCatImportDiscovery selected_container = {0};
    CHECK(bongo_cat_import_discover(root, &selected_container, &error));
    CHECK(selected_container.count == 3);
    size_t patch_count = 0;
    for (size_t i = 0; i < selected_container.count; ++i)
        if (selected_container.candidates[i].format ==
            BONGO_CAT_IMPORT_MVER_PATCH) patch_count++;
    CHECK(patch_count == 1);
    CHECK(bongo_cat_model_remove_tree(root, NULL));
    SDL_free(temporary);
}
