#include "preferences_model_cover.h"
#include "preferences_internal.h"
#include "runtime.h"
#include "bongo_cat/file.h"
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

typedef struct ModelCoverAttempt {
    BongoCatApp *app;
    char model_id[BONGO_CAT_ID_CAP];
    bool attempted;
} ModelCoverAttempt;

static ModelCoverSlot cover_cache[BONGO_CAT_MODEL_CAP];
static ModelCoverAttempt cover_attempts[BONGO_CAT_MODEL_CAP];
static uint64_t cover_generation;

static void clear_attempts(BongoCatApp *app) {
    for (size_t i = 0; i < BONGO_CAT_MODEL_CAP; ++i)
        if (!app || cover_attempts[i].app == app)
            memset(&cover_attempts[i], 0, sizeof(cover_attempts[i]));
}

static void prune_attempts(BongoCatApp *app) {
    if (!app) return;
    for (size_t i = 0; i < BONGO_CAT_MODEL_CAP; ++i) {
        ModelCoverAttempt *attempt = &cover_attempts[i];
        if (attempt->app == app && !bongo_cat_models_find(
            &app->models, attempt->model_id))
            memset(attempt, 0, sizeof(*attempt));
    }
}

void bongo_cat_preferences_model_cover_cache_clear(BongoCatApp *app) {
    for (size_t i = 0; i < BONGO_CAT_MODEL_CAP; ++i) {
        ModelCoverSlot *slot = &cover_cache[i];
        if ((!app || slot->app == app) && slot->image.texture)
            glDeleteTextures(1, &slot->image.texture);
        if (!app || slot->app == app) memset(slot, 0, sizeof(*slot));
    }
}

void bongo_cat_preferences_model_cache_clear(BongoCatApp *app) {
    bongo_cat_preferences_remove_dialog_clear(app);
    bongo_cat_preferences_model_cover_cache_clear(app);
    clear_attempts(app);
}

void bongo_cat_preferences_model_covers_begin(BongoCatApp *app) {
    cover_generation++;
    if (!cover_generation) cover_generation++;
    prune_attempts(app);
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

static bool capture_model_cover(BongoCatApp *app,
    const BongoCatModelEntry *entry) {
    if (!app || !entry || !app->window || !app->gl_context ||
        app->window_minimized) return false;
    char path[BONGO_CAT_PATH_CAP], resources[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(resources, sizeof(resources),
        entry->adapter_directory, "resources") ||
        !bongo_cat_path_join(path, sizeof(path), resources, "cover.png")) return false;
    char marker[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(marker, sizeof(marker), resources,
        ".bongo-cat-cover-fallback")) return false;
    bool fallback = bongo_cat_path_is_file(marker);
    if (bongo_cat_path_is_file(path) && !fallback) return true;
    if (!bongo_cat_path_create_directory(resources)) return false;
    SDL_Window *previous_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
    char temporary[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(temporary, sizeof(temporary), resources,
        ".bongo-cat-runtime-cover.png")) return false;
    bongo_cat_file_remove(temporary);
    snprintf(app->pending_model_cover_path,
        sizeof(app->pending_model_cover_path), "%s", temporary);
    bongo_cat_app_capture_pending_frame(app);
    app->pending_model_cover_path[0] = '\0';
    if (previous_window && previous_context &&
        (previous_window != SDL_GL_GetCurrentWindow() ||
            previous_context != SDL_GL_GetCurrentContext()))
        SDL_GL_MakeCurrent(previous_window, previous_context);
    if (!bongo_cat_path_is_file(temporary)) return false;
    if (!bongo_cat_file_replace(temporary, path, true)) {
        bongo_cat_file_remove(temporary);
        return false;
    }
    if (fallback) bongo_cat_file_remove(marker);
    return true;
}

static bool cover_needs_capture(const BongoCatModelEntry *entry) {
    char resources[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
    char marker[BONGO_CAT_PATH_CAP];
    return entry && bongo_cat_path_join(resources, sizeof(resources),
            entry->adapter_directory, "resources") &&
        bongo_cat_path_join(path, sizeof(path), resources, "cover.png") &&
        bongo_cat_path_join(marker, sizeof(marker), resources,
            ".bongo-cat-cover-fallback") &&
        (!bongo_cat_path_is_file(path) || bongo_cat_path_is_file(marker));
}

static ModelCoverAttempt *find_cover_attempt(BongoCatApp *app,
    const char *model_id) {
    for (size_t i = 0; i < BONGO_CAT_MODEL_CAP; ++i)
        if (cover_attempts[i].app == app &&
            !strcmp(cover_attempts[i].model_id, model_id))
            return &cover_attempts[i];
    return NULL;
}

static ModelCoverAttempt *create_cover_attempt(BongoCatApp *app,
    const char *model_id) {
    for (size_t i = 0; i < BONGO_CAT_MODEL_CAP; ++i) {
        ModelCoverAttempt *attempt = &cover_attempts[i];
        if (attempt->app) continue;
        attempt->app = app;
        snprintf(attempt->model_id, sizeof(attempt->model_id), "%s", model_id);
        return attempt;
    }
    return NULL;
}

static void forget_cached_cover(BongoCatApp *app,
    const BongoCatModelEntry *entry) {
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), entry->adapter_directory,
        "resources/cover.png")) return;
    for (size_t i = 0; i < BONGO_CAT_MODEL_CAP; ++i) {
        ModelCoverSlot *slot = &cover_cache[i];
        if (slot->app != app || strcmp(slot->path, path)) continue;
        if (slot->image.texture) glDeleteTextures(1, &slot->image.texture);
        memset(slot, 0, sizeof(*slot));
        return;
    }
}

bool bongo_cat_preferences_model_cover_generate_current(BongoCatApp *app) {
    if (!app || !app->window || !app->gl_context ||
        app->window_minimized) return false;
    const BongoCatModelEntry *entry = bongo_cat_models_find(
        &app->models, app->loaded_model);
    if (!entry || strcmp(app->session.active_model_id, entry->id) ||
        !cover_needs_capture(entry))
        return false;
    ModelCoverAttempt *attempt = find_cover_attempt(app, entry->id);
    if (!attempt) {
        attempt = create_cover_attempt(app, entry->id);
        if (!attempt) return false;
        return true;
    }
    if (attempt->attempted) return false;
    attempt->attempted = true;
    bool captured = capture_model_cover(app, entry);
    if (captured) forget_cached_cover(app, entry);
    else SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "Cannot generate cover for model %s", entry->id);
    return true;
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
