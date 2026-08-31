#include "model_cover.h"

#include "preferences_model_cover.h"
#include "runtime.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stb_image_write.h>

typedef struct CoverWriter {
    FILE *file;
    bool ok;
} CoverWriter;

static bool cover_path(char *path, size_t capacity,
    const char *adapter, const char *relative) {
    return adapter && adapter[0] && bongo_cat_path_join(path, capacity,
        adapter, relative);
}

static bool marker_exists(const char *adapter, const char *relative) {
    char path[BONGO_CAT_PATH_CAP];
    return cover_path(path, sizeof(path), adapter, relative) &&
        bongo_cat_path_is_file(path);
}

static bool write_marker(const char *adapter, const char *relative) {
    char path[BONGO_CAT_PATH_CAP];
    if (!cover_path(path, sizeof(path), adapter, relative)) return false;
    FILE *file = bongo_cat_file_open(path, "wb");
    if (!file) return false;
    return fclose(file) == 0;
}

static void remove_marker(const char *adapter, const char *relative) {
    char path[BONGO_CAT_PATH_CAP];
    if (cover_path(path, sizeof(path), adapter, relative))
        bongo_cat_file_remove(path);
}

static bool task_matches_loaded(const BongoCatApp *app, size_t index) {
    return app && index < app->pending_model_cover_count &&
        app->loaded_model[0] &&
        strcmp(app->pending_model_cover_ids[index], app->loaded_model) == 0;
}

static void discard_stale_tasks(BongoCatApp *app) {
    if (!app) return;
    size_t first = 0;
    while (first < app->pending_model_cover_count &&
        !task_matches_loaded(app, first)) {
        SDL_Log("Discarding stale model cover task: model=%s current=%s",
            app->pending_model_cover_ids[first], app->loaded_model[0] ?
                app->loaded_model : "none");
        first++;
    }
    if (!first) return;
    size_t remaining = app->pending_model_cover_count - first;
    memmove(app->pending_model_cover_ids,
        app->pending_model_cover_ids + first,
        remaining * sizeof(app->pending_model_cover_ids[0]));
    memmove(app->pending_model_cover_paths,
        app->pending_model_cover_paths + first,
        remaining * sizeof(app->pending_model_cover_paths[0]));
    app->pending_model_cover_count = remaining;
}

static void remove_first_task(BongoCatApp *app) {
    if (!app || !app->pending_model_cover_count) return;
    size_t remaining = --app->pending_model_cover_count;
    if (remaining) {
        memmove(app->pending_model_cover_ids,
            app->pending_model_cover_ids + 1,
            remaining * sizeof(app->pending_model_cover_ids[0]));
        memmove(app->pending_model_cover_paths,
            app->pending_model_cover_paths + 1,
            remaining * sizeof(app->pending_model_cover_paths[0]));
    }
    app->pending_model_cover_ids[app->pending_model_cover_count][0] = '\0';
    app->pending_model_cover_paths[app->pending_model_cover_count][0] = '\0';
}

void bongo_cat_model_cover_schedule(BongoCatApp *app,
    const BongoCatModelEntry *entry) {
    if (!app || !entry || !entry->adapter_directory[0]) return;
    bool fallback = marker_exists(entry->adapter_directory,
        BONGO_CAT_MODEL_COVER_FALLBACK_MARKER);
    bool generated = marker_exists(entry->adapter_directory,
        BONGO_CAT_MODEL_COVER_GENERATED_MARKER);
    if (!fallback && !generated) return;
    char path[BONGO_CAT_PATH_CAP];
    if (!cover_path(path, sizeof(path), entry->adapter_directory,
            BONGO_CAT_MODEL_COVER_FILE)) return;
    for (size_t i = 0; i < app->pending_model_cover_count; ++i)
        if (strcmp(app->pending_model_cover_ids[i], entry->id) == 0 ||
            strcmp(app->pending_model_cover_paths[i], path) == 0) return;
    if (app->pending_model_cover_count >= BONGO_CAT_MODEL_COVER_PENDING_CAP) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Model cover queue is full; dropping refresh: id=%s", entry->id);
        return;
    }
    size_t index = app->pending_model_cover_count++;
    snprintf(app->pending_model_cover_ids[index],
        sizeof(app->pending_model_cover_ids[index]), "%s", entry->id);
    snprintf(app->pending_model_cover_paths[index],
        sizeof(app->pending_model_cover_paths[index]), "%s", path);
    SDL_Log("Scheduling model cover refresh: id=%s source=%s path=%s",
        entry->id, generated ? "generated" : "fallback", path);
    app->dirty = true;
}

bool bongo_cat_model_cover_pending(const BongoCatApp *app) {
    if (!app || !app->loaded_model[0]) return false;
    for (size_t i = 0; i < app->pending_model_cover_count; ++i)
        if (strcmp(app->pending_model_cover_ids[i], app->loaded_model) == 0)
            return true;
    return false;
}

const char *bongo_cat_model_cover_pending_path(const BongoCatApp *app) {
    if (!app || !app->loaded_model[0]) return NULL;
    for (size_t i = 0; i < app->pending_model_cover_count; ++i)
        if (strcmp(app->pending_model_cover_ids[i], app->loaded_model) == 0)
            return app->pending_model_cover_paths[i];
    return NULL;
}

void bongo_cat_model_cover_capture_before_switch(BongoCatApp *app) {
    if (!app || !app->loaded_model[0]) return;
    const BongoCatModelEntry *entry = bongo_cat_models_find(&app->models,
        app->loaded_model);
    if (!entry) return;
    bongo_cat_model_cover_schedule(app, entry);
    if (!bongo_cat_model_cover_pending(app)) return;
    if (!bongo_cat_app_capture_pending_model_cover(app))
        SDL_Log("Model cover refresh before switch deferred: id=%s",
            entry->id);
}

static void cover_write(void *context, void *data, int size) {
    CoverWriter *writer = context;
    if (writer->ok && fwrite(data, 1, (size_t)size, writer->file) !=
            (size_t)size)
        writer->ok = false;
}

void bongo_cat_model_cover_capture(BongoCatApp *app, int width, int height) {
    if (!app || !app->pending_model_cover_count) return;
    discard_stale_tasks(app);
    if (!app->pending_model_cover_count) return;
    uint64_t started_ns = SDL_GetTicksNS();
    char path[BONGO_CAT_PATH_CAP];
    snprintf(path, sizeof(path), "%s", app->pending_model_cover_paths[0]);
    remove_first_task(app);
    /* Keep the desktop's native pixel size while bounding readback memory. */
    if (width < 2 || height < 2 || width > 4096 || height > 4096) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Model cover refresh skipped for unsupported size %dx%d: %s",
            width, height, path);
        return;
    }
    size_t pitch = (size_t)width * 4;
    size_t bytes = pitch * (size_t)height;
    if (pitch / 4 != (size_t)width || bytes / pitch != (size_t)height) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Model cover refresh skipped due to size overflow: %s", path);
        return;
    }
    unsigned char *pixels = malloc(bytes);
    if (!pixels) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Model cover refresh allocation failed for %zu bytes: %s",
            bytes, path);
        return;
    }
    GLenum before = GL_NO_ERROR;
    GLenum stale;
    while ((stale = glGetError()) != GL_NO_ERROR) before = stale;
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    GLenum after = glGetError();
    if (after != GL_NO_ERROR) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Model cover framebuffer readback failed: path=%s "
            "gl_error_before=0x%x gl_error_after=0x%x", path, before, after);
        free(pixels);
        return;
    }
    for (int y = 0; y < height / 2; ++y) {
        unsigned char *top = pixels + (size_t)y * pitch;
        unsigned char *bottom = pixels + (size_t)(height - y - 1) * pitch;
        for (size_t i = 0; i < pitch; ++i) {
            unsigned char value = top[i];
            top[i] = bottom[i];
            bottom[i] = value;
        }
    }
    FILE *file = bongo_cat_file_open(path, "wb");
    CoverWriter writer = {file, file != NULL};
    bool ok = file && stbi_write_png_to_func(cover_write, &writer,
        width, height, 4, pixels, (int)pitch) && writer.ok;
    if (file && fclose(file) != 0) ok = false;
    if (!ok) {
        bongo_cat_file_remove(path);
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Model cover could not be written: %s", path);
        free(pixels);
        return;
    }
    char adapter[BONGO_CAT_PATH_CAP];
    snprintf(adapter, sizeof(adapter), "%s", path);
    char *separator = strrchr(adapter, '/');
    if (!separator) separator = strrchr(adapter, '\\');
    if (separator) *separator = '\0';
    bool marked_generated = write_marker(adapter,
        BONGO_CAT_MODEL_COVER_GENERATED_NAME);
    if (marked_generated)
        remove_marker(adapter, BONGO_CAT_MODEL_COVER_FALLBACK_NAME);
    else
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Model cover written but generated marker could not be created: %s",
            adapter);
    if (app->preferences)
        bongo_cat_preferences_model_cover_reload(app, path);
    SDL_Log("Model cover refreshed: path=%s (%dx%d) elapsed_ms=%.2f", path,
        width, height, (double)(SDL_GetTicksNS() - started_ns) / 1000000.0);
    free(pixels);
}
