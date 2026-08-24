#include "preferences_model_cover.h"
#include "preferences_internal.h"
#include "runtime.h"
#include "bongo_cat/file.h"
#include "bongo_cat/image.h"
#include "bongo_cat/path.h"
#include "bongo_cat/preferences.h"

#include <SDL3/SDL_opengl.h>
#include <stdio.h>
#include <string.h>

typedef struct ModelCoverSlot {
    BongoCatModelCover image;
    BongoCatApp *app;
    char path[BONGO_CAT_PATH_CAP];
    int requested_width;
    int requested_height;
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
static unsigned cover_load_budget;
static bool cover_load_deferred;

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
    cover_load_budget = 1;
    cover_load_deferred = false;
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
            if (slot->requested_width == pixel_width &&
                slot->requested_height == pixel_height) {
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
    if (!cover_load_budget) {
        cover_load_deferred = true;
        return NULL;
    }
    cover_load_budget--;
    BongoCatError ignored = {0};
    empty->image.texture = bongo_cat_image_texture_thumbnail(path,
        pixel_width, pixel_height, &empty->image.width,
        &empty->image.height, &ignored);
    if (!empty->image.texture) return NULL;
    empty->app = app; empty->generation = cover_generation;
    empty->requested_width = pixel_width;
    empty->requested_height = pixel_height;
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
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(app->window, &width, &height);
    BongoCatLive2D *cover = NULL;
    BongoCatError ignored = {0};
    if (width > 1 && height > 1 &&
        SDL_GL_MakeCurrent(app->window, app->gl_context)) {
        /* Generated-cover state is destructive, so it requires its own model. */
        cover = bongo_cat_live2d_create_cover_runtime(
            app->asset_root, &ignored);
        if (cover) {
            bongo_cat_live2d_reshape(cover, width, height);
            if (bongo_cat_live2d_load(cover, entry->directory,
                entry->setting_file, entry->preset,
                &app->model_render_options, NULL, NULL,
                &ignored) != BONGO_CAT_OK) {
                bongo_cat_live2d_destroy(cover);
                cover = NULL;
            }
        }
    }
    if (cover && cover != app->live2d &&
        bongo_cat_live2d_prepare_cover(cover)) {
        bongo_cat_live2d_resize(cover, width, height);
        glViewport(0, 0, width, height);
        glEnable(GL_MULTISAMPLE);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        int x = 0, y = 0, content_width = width, content_height = height;
        if (bongo_cat_live2d_viewport(cover, &x, &y,
            &content_width, &content_height) &&
            content_width > 0 && content_height > 0)
            glViewport(x, y, content_width, content_height);
        bongo_cat_live2d_set_mirror(cover, app->settings.model.mirror);
        bongo_cat_live2d_draw(cover);
        glViewport(0, 0, width, height);
        snprintf(app->pending_model_cover_path,
            sizeof(app->pending_model_cover_path), "%s", temporary);
        bongo_cat_frame_capture_pending(app, width, height);
        app->dirty = true;
    }
    app->pending_model_cover_path[0] = '\0';
    if (cover) bongo_cat_live2d_destroy(cover);
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
    if (cover_load_deferred && app)
        bongo_cat_preferences_invalidate(app->preferences);
}
