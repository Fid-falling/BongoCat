#include "preferences_model_cover.h"
#include "preferences_internal.h"
#include "bongo_cat/image.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL_opengl.h>
#include <stdio.h>
#include <string.h>

typedef struct ModelCoverSlot {
    BongoCatModelCover image;
    BongoCatApp *app;
    char path[BONGO_CAT_PATH_CAP];
    uint64_t generation;
} ModelCoverSlot;

static ModelCoverSlot cover_cache[BONGO_CAT_MODEL_CAP];
static uint64_t cover_generation;

void bongo_cat_preferences_model_cache_clear(BongoCatApp *app) {
    bongo_cat_preferences_remove_dialog_clear(app);
    for (size_t i = 0; i < BONGO_CAT_MODEL_CAP; ++i) {
        ModelCoverSlot *slot = &cover_cache[i];
        if ((!app || slot->app == app) && slot->image.texture)
            glDeleteTextures(1, &slot->image.texture);
        if (!app || slot->app == app) memset(slot, 0, sizeof(*slot));
    }
}

void bongo_cat_preferences_model_covers_begin(void) {
    cover_generation++;
    if (!cover_generation) cover_generation++;
}

const BongoCatModelCover *bongo_cat_preferences_model_cover(
    BongoCatApp *app, const BongoCatModelEntry *entry,
    int pixel_width, int pixel_height) {
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), entry->adapter_directory,
        "resources/cover.png") || !bongo_cat_path_is_file(path)) return NULL;
    ModelCoverSlot *empty = NULL;
    for (size_t i = 0; i < BONGO_CAT_MODEL_CAP; ++i) {
        ModelCoverSlot *slot = &cover_cache[i];
        if (slot->image.texture && slot->app == app && !strcmp(slot->path, path)) {
            if (slot->image.width == pixel_width ||
                slot->image.height == pixel_height) {
                slot->generation = cover_generation; return &slot->image;
            }
            glDeleteTextures(1, &slot->image.texture);
            memset(slot, 0, sizeof(*slot));
            empty = slot;
            break;
        }
        if (!slot->image.texture && !empty) empty = slot;
    }
    if (!empty) return NULL;
    BongoCatError ignored = {0};
    empty->image.texture = bongo_cat_image_texture_resampled(path,
        pixel_width, pixel_height, 0, &empty->image.width,
        &empty->image.height, &ignored);
    if (!empty->image.texture) return NULL;
    empty->app = app; empty->generation = cover_generation;
    snprintf(empty->path, sizeof(empty->path), "%s", path);
    return &empty->image;
}

void bongo_cat_preferences_model_covers_prune(BongoCatApp *app) {
    for (size_t i = 0; i < BONGO_CAT_MODEL_CAP; ++i) {
        ModelCoverSlot *slot = &cover_cache[i];
        if (slot->app != app || !slot->image.texture ||
            slot->generation == cover_generation) continue;
        glDeleteTextures(1, &slot->image.texture);
        memset(slot, 0, sizeof(*slot));
    }
}
