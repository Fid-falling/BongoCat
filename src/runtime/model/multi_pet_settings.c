#include "runtime.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"
#include "bongo_cat/preferences.h"

#include <stdio.h>

#define PASS_THROUGH_REQUEST_FILE "pass-through.request"

static bool request_path(char *target, size_t capacity, const char *root,
    const char *model_id) {
    char directory[BONGO_CAT_PATH_CAP];
    return bongo_cat_multi_pet_state_directory(directory, sizeof(directory),
            root, model_id) &&
        bongo_cat_path_join(target, capacity, directory,
            PASS_THROUGH_REQUEST_FILE);
}

void bongo_cat_multi_pet_clear_pass_through_request(BongoCatApp *app,
    const char *model_id) {
    char path[BONGO_CAT_PATH_CAP];
    if (app && request_path(path, sizeof(path), app->primary_state_root,
            model_id) && bongo_cat_path_is_file(path) &&
        !bongo_cat_file_remove(path))
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "[runtime] Stale multi-pet pass-through request could not be "
            "removed: model=%s path=%s", model_id, path);
}

bool bongo_cat_multi_pet_request_pass_through(BongoCatApp *app,
    bool enabled) {
    char path[BONGO_CAT_PATH_CAP], temporary[BONGO_CAT_PATH_CAP];
    int length;
    if (!app || !app->secondary_pet ||
        !bongo_cat_path_join(path, sizeof(path), app->state_root,
            PASS_THROUGH_REQUEST_FILE) ||
        (length = snprintf(temporary, sizeof(temporary), "%s.tmp", path)) < 0 ||
        (size_t)length >= sizeof(temporary)) return false;
    FILE *file = bongo_cat_file_open(temporary, "wb");
    bool written = file && fprintf(file, "%d\n", enabled) > 0;
    if (file && fclose(file) != 0) written = false;
    if (!written || !bongo_cat_file_replace(temporary, path, true)) {
        bongo_cat_file_remove(temporary);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "[runtime] Multi-pet pass-through request failed: model=%s "
            "path=%s", app->secondary_model_id, path);
        return false;
    }
    return true;
}

static bool take_request(BongoCatApp *app, const char *model_id,
    bool *enabled) {
    char path[BONGO_CAT_PATH_CAP], content[8] = {0};
    if (!request_path(path, sizeof(path), app->primary_state_root, model_id) ||
        !bongo_cat_path_is_file(path)) return false;
    FILE *file = bongo_cat_file_open(path, "rb");
    if (!file) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "[runtime] Multi-pet pass-through request could not be read: "
            "model=%s path=%s", model_id, path);
        return false;
    }
    size_t length = file ? fread(content, 1, sizeof(content) - 1, file) : 0;
    if (file) fclose(file);
    if (!bongo_cat_file_remove(path)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "[runtime] Multi-pet pass-through request could not be consumed: "
            "model=%s path=%s", model_id, path);
        return false;
    }
    content[length] = '\0';
    bool valid = content[0] == '0' || content[0] == '1';
    for (size_t i = 1; valid && content[i]; ++i)
        valid = content[i] == ' ' || content[i] == '\t' ||
            content[i] == '\r' || content[i] == '\n';
    if (!valid) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "[runtime] Multi-pet pass-through request invalid: model=%s "
            "path=%s content=%s", model_id, path, content);
        return false;
    }
    *enabled = content[0] == '1';
    return true;
}

void bongo_cat_multi_pet_pass_through_requests_update(BongoCatApp *app) {
    if (!app || app->secondary_pet) return;
    bool changed = false;
    for (size_t i = 0; i < app->session.additional_model_count; ++i) {
        bool enabled;
        const char *model_id = app->session.additional_model_ids[i];
        if (!take_request(app, model_id, &enabled)) continue;
        app->settings.window.pass_through = enabled;
        changed = true;
    }
    if (!changed) return;
    bongo_cat_window_mark_hit_dirty(app);
    bongo_cat_window_sync_click_through(app);
    bongo_cat_preferences_invalidate(app->preferences);
}
