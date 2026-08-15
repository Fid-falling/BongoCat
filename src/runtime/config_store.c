#include "runtime.h"
#include "bongo_cat/path.h"

#include <string.h>

#define SAVE_DELAY_NS 300000000ull
#define RETRY_DELAY_NS 1000000000ull

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size) {
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static uint64_t settings_hash(const BongoCatSettings *settings) {
    uint64_t hash = 1469598103934665603ull;
#define HASH_FIELD(field) hash = hash_bytes(hash, &(field), sizeof(field))
    HASH_FIELD(settings->model);
    HASH_FIELD(settings->window);
    HASH_FIELD(settings->app);
    HASH_FIELD(settings->shortcuts);
    HASH_FIELD(settings->behavior_shortcut_count);
    hash = hash_bytes(hash, settings->behavior_shortcuts,
        settings->behavior_shortcut_count * sizeof(settings->behavior_shortcuts[0]));
    HASH_FIELD(settings->model_label_count);
    hash = hash_bytes(hash, settings->model_labels,
        settings->model_label_count * sizeof(settings->model_labels[0]));
    HASH_FIELD(settings->extensions_json);
#undef HASH_FIELD
    return hash;
}

static uint64_t session_hash(const BongoCatSessionState *session) {
    uint64_t hash = 1469598103934665603ull;
#define HASH_FIELD(field) hash = hash_bytes(hash, &(field), sizeof(field))
    HASH_FIELD(session->window.visible);
    HASH_FIELD(session->window.position_known);
    HASH_FIELD(session->window.scale_percent);
    HASH_FIELD(session->window.opacity_percent);
    HASH_FIELD(session->window.x);
    HASH_FIELD(session->window.y);
    HASH_FIELD(session->window.width);
    HASH_FIELD(session->window.height);
    HASH_FIELD(session->active_model_id);
#undef HASH_FIELD
    return hash;
}

void bongo_cat_config_store_initialize(BongoCatApp *app) {
    uint64_t settings = settings_hash(&app->settings);
    uint64_t session = session_hash(&app->session);
    app->settings_observed_hash = settings;
    app->session_observed_hash = session;
    app->settings_saved_hash = app->settings_store_valid ? settings : 0;
    app->session_saved_hash = app->session_store_valid ? session : 0;
}

static bool save_settings(BongoCatApp *app, uint64_t hash) {
    BongoCatError error = {0};
    if (bongo_cat_settings_save(app->settings_path,
        &app->settings, &error) != BONGO_CAT_OK) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
        return false;
    }
    app->settings_saved_hash = hash;
    return true;
}

static bool save_session(BongoCatApp *app, uint64_t hash) {
    BongoCatError error = {0};
    if (bongo_cat_session_save(app->session_path,
        &app->session, &error) != BONGO_CAT_OK) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
        return false;
    }
    app->session_saved_hash = hash;
    return true;
}

void bongo_cat_config_store_update(BongoCatApp *app, uint64_t now) {
    if (!app || app->smoke || !app->settings_path[0] || !app->session_path[0]) return;
    uint64_t settings = settings_hash(&app->settings);
    uint64_t session = session_hash(&app->session);
    if (settings != app->settings_observed_hash) {
        app->settings_observed_hash = settings;
        app->settings_save_due_ns = now + SAVE_DELAY_NS;
    } else if (settings != app->settings_saved_hash &&
        !app->settings_save_due_ns) app->settings_save_due_ns = now + SAVE_DELAY_NS;
    if (session != app->session_observed_hash) {
        app->session_observed_hash = session;
        app->session_save_due_ns = now + SAVE_DELAY_NS;
    } else if (session != app->session_saved_hash &&
        !app->session_save_due_ns) app->session_save_due_ns = now + SAVE_DELAY_NS;
    if (app->settings_save_due_ns && now >= app->settings_save_due_ns)
        app->settings_save_due_ns = save_settings(app, settings)
            ? 0 : now + RETRY_DELAY_NS;
    if (app->session_save_due_ns && now >= app->session_save_due_ns)
        app->session_save_due_ns = save_session(app, session)
            ? 0 : now + RETRY_DELAY_NS;
}

void bongo_cat_config_store_flush(BongoCatApp *app) {
    if (!app || app->smoke || !app->settings_path[0] || !app->session_path[0]) return;
    uint64_t settings = settings_hash(&app->settings);
    uint64_t session = session_hash(&app->session);
    if (settings != app->settings_saved_hash) save_settings(app, settings);
    if (session != app->session_saved_hash) save_session(app, session);
    app->settings_observed_hash = settings;
    app->session_observed_hash = session;
    app->settings_save_due_ns = app->session_save_due_ns = 0;
}
