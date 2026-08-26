#include "runtime.h"
#include "bongo_cat/preferences.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct BongoCatModelRefreshJob {
    struct BongoCatModelRefresh *owner;
    BongoCatModelCatalog models;
    char asset_root[BONGO_CAT_PATH_CAP];
    char models_root[BONGO_CAT_PATH_CAP];
    char cache_root[BONGO_CAT_PATH_CAP];
    char nearby_root[BONGO_CAT_PATH_CAP];
    char active_model_id[BONGO_CAT_ID_CAP];
    uint64_t started_ns;
    uint64_t revision;
    bool success;
} BongoCatModelRefreshJob;

struct BongoCatModelRefresh {
    SDL_Mutex *mutex;
    SDL_Thread *worker;
    BongoCatModelRefreshJob *completed;
    Uint32 event_type;
    uint64_t revision;
    bool busy;
    bool rerun;
};

static BongoCatModelRefresh *create_refresh(void) {
    BongoCatModelRefresh *refresh = calloc(1, sizeof(*refresh));
    if (!refresh) return NULL;
    refresh->mutex = SDL_CreateMutex();
    refresh->event_type = SDL_RegisterEvents(1);
    if (!refresh->mutex || refresh->event_type == (Uint32)-1) {
        if (refresh->mutex) SDL_DestroyMutex(refresh->mutex);
        free(refresh);
        return NULL;
    }
    return refresh;
}

static int SDLCALL refresh_worker(void *userdata) {
    BongoCatModelRefreshJob *job = userdata;
    BongoCatApp *scan = calloc(1, sizeof(*scan));
    if (scan) {
        snprintf(scan->asset_root, sizeof(scan->asset_root), "%s",
            job->asset_root);
        snprintf(scan->models_root, sizeof(scan->models_root), "%s",
            job->models_root);
        snprintf(scan->cache_root, sizeof(scan->cache_root), "%s",
            job->cache_root);
        snprintf(scan->session.active_model_id,
            sizeof(scan->session.active_model_id), "%s",
            job->active_model_id);
        bongo_cat_model_catalog_scan(scan, false, job->nearby_root);
        job->models = scan->models;
        job->success = true;
        free(scan);
    }

    BongoCatModelRefresh *refresh = job->owner;
    SDL_LockMutex(refresh->mutex);
    refresh->completed = job;
    SDL_UnlockMutex(refresh->mutex);

    SDL_Event event = {0};
    event.type = refresh->event_type;
    event.user.data1 = refresh;
    SDL_PushEvent(&event);
    return 0;
}

static bool start_refresh(BongoCatApp *app) {
    BongoCatModelRefresh *refresh = app->model_refresh;
    BongoCatModelRefreshJob *job = calloc(1, sizeof(*job));
    if (!job) return false;
    job->owner = refresh;
    job->started_ns = SDL_GetTicksNS();
    job->revision = refresh->revision;
    snprintf(job->asset_root, sizeof(job->asset_root), "%s", app->asset_root);
    snprintf(job->models_root, sizeof(job->models_root), "%s", app->models_root);
    snprintf(job->cache_root, sizeof(job->cache_root), "%s", app->cache_root);
    snprintf(job->nearby_root, sizeof(job->nearby_root), "%s",
        app->nearby_root);
    snprintf(job->active_model_id, sizeof(job->active_model_id), "%s",
        app->session.active_model_id);
    refresh->busy = true;
    refresh->rerun = false;
    refresh->worker = SDL_CreateThread(refresh_worker,
        "bongo-cat-model-catalog", job);
    if (refresh->worker) return true;
    refresh->busy = false;
    free(job);
    return false;
}

void bongo_cat_app_request_nearby_model_refresh(BongoCatApp *app) {
    if (!app || SDL_getenv("BONGO_CAT_DISABLE_NEARBY_MODEL_SCAN")) return;
    if (!app->model_refresh) app->model_refresh = create_refresh();
    BongoCatModelRefresh *refresh = app->model_refresh;
    if (!refresh) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Background model refresh is unavailable: %s", SDL_GetError());
        return;
    }
    if (refresh->busy) {
        refresh->rerun = true;
        return;
    }
    if (!start_refresh(app))
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Cannot start background model refresh: %s", SDL_GetError());
}

void bongo_cat_model_refresh_invalidate(BongoCatApp *app) {
    BongoCatModelRefresh *refresh = app ? app->model_refresh : NULL;
    if (!refresh) return;
    refresh->revision++;
    if (refresh->busy) refresh->rerun = true;
}

void bongo_cat_model_refresh_update(BongoCatApp *app) {
    BongoCatModelRefresh *refresh = app ? app->model_refresh : NULL;
    if (!refresh || !refresh->busy) return;
    SDL_LockMutex(refresh->mutex);
    BongoCatModelRefreshJob *job = refresh->completed;
    refresh->completed = NULL;
    SDL_UnlockMutex(refresh->mutex);
    if (!job) return;

    if (refresh->worker) SDL_WaitThread(refresh->worker, NULL);
    refresh->worker = NULL;
    refresh->busy = false;
    if (job->success && job->revision == refresh->revision) {
        bool changed = app->models.count != job->models.count ||
            memcmp(app->models.entries, job->models.entries,
                job->models.count * sizeof(job->models.entries[0])) != 0;
        if (changed) {
            app->models = job->models;
            bongo_cat_model_catalog_finish(app);
            bongo_cat_preferences_models_changed(app->preferences);
        }
        SDL_Log("Background model refresh completed: models=%llu changed=%d "
            "elapsed_ms=%.1f", (unsigned long long)job->models.count,
            changed, (SDL_GetTicksNS() - job->started_ns) / 1000000.0);
    } else if (!job->success)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Background model refresh failed: not enough memory");
    free(job);
    if (refresh->rerun && !start_refresh(app))
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Cannot restart background model refresh: %s", SDL_GetError());
}

bool bongo_cat_model_refresh_event(BongoCatApp *app,
    const SDL_Event *event) {
    BongoCatModelRefresh *refresh = app ? app->model_refresh : NULL;
    if (!refresh || !event || event->type != refresh->event_type ||
        event->user.data1 != refresh) return false;
    bongo_cat_model_refresh_update(app);
    return true;
}

void bongo_cat_model_refresh_shutdown(BongoCatApp *app) {
    BongoCatModelRefresh *refresh = app ? app->model_refresh : NULL;
    if (!refresh) return;
    if (refresh->worker) SDL_WaitThread(refresh->worker, NULL);
    SDL_LockMutex(refresh->mutex);
    BongoCatModelRefreshJob *job = refresh->completed;
    refresh->completed = NULL;
    SDL_UnlockMutex(refresh->mutex);
    free(job);
    SDL_Event event;
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, refresh->event_type,
        refresh->event_type) > 0) {}
    SDL_DestroyMutex(refresh->mutex);
    free(refresh);
    app->model_refresh = NULL;
}
