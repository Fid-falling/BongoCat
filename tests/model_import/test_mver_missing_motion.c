#include "test_mver_support.h"

#include "model_import.h"
#include "model_storage.h"

#include <SDL3/SDL.h>
#include <stdio.h>

static int failures;
#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #value); \
    failures++; \
} } while (0)

int test_mver_missing_motion_groups(void) {
    failures = 0;
    char *temporary = SDL_GetCurrentDirectory();
    CHECK(temporary != NULL);
    if (!temporary) return failures;
    unsigned long long stamp = (unsigned long long)SDL_GetTicksNS();
    char source[BONGO_CAT_PATH_CAP], data[BONGO_CAT_PATH_CAP];
    char path[BONGO_CAT_PATH_CAP];
    snprintf(source, sizeof(source), "%s/bongo-cat-stale-motion-%llu",
        temporary, stamp);
    snprintf(data, sizeof(data), "%s/bongo-cat-stale-motion-data-%llu",
        temporary, stamp);
    CHECK(mver_fixture(source));
    CHECK(SDL_CreateDirectory(data));
    CHECK(child(path, sizeof(path), source, "config.json", false));
    CHECK(write_text(path, "{\"standard\":{\"hand\":[[65]],"
        "\"keyboard\":[[65]],\"l2d_motion\":[[67]],"
        "\"l2d_motion_lockhand\":[[68]]}}"));
    BongoCatImportReceipt receipt = {0};
    BongoCatError error = {0};
    CHECK(bongo_cat_import_install(source, data, &receipt, &error) ==
        BONGO_CAT_OK);
    CHECK(receipt.count == 1 && receipt.installed_count == 1);
    CHECK(bongo_cat_model_remove_tree(source, NULL));
    CHECK(bongo_cat_model_remove_tree(data, NULL));
    SDL_free(temporary);
    return failures;
}
