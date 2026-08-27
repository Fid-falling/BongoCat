#include "update_internal.h"
#include "preferences_notice.h"
#include "bongo_cat/i18n.h"
#include "bongo_cat/preferences.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RELEASES_URL \
    "https://github.com/vladelaina/BongoCat/releases/latest"
#define STORE_URI "ms-windows-store://pdp/?ProductId=9NX7G84J3WM6"
#define STORE_WEB_URL "https://apps.microsoft.com/detail/9NX7G84J3WM6"

static void reap_worker(BongoCatUpdateService *service) {
    SDL_LockMutex(service->mutex);
    SDL_Thread *worker = service->status != BONGO_CAT_UPDATE_CHECKING
        ? service->worker : NULL;
    if (worker) service->worker = NULL;
    SDL_UnlockMutex(service->mutex);
    if (worker) SDL_WaitThread(worker, NULL);
}

static int local_day(void) {
    time_t timestamp = time(NULL);
    struct tm local = {0};
#ifdef _WIN32
    if (localtime_s(&local, &timestamp) != 0) return 0;
#else
    if (!localtime_r(&timestamp, &local)) return 0;
#endif
    return (local.tm_year + 1900) * 10000 +
        (local.tm_mon + 1) * 100 + local.tm_mday;
}

static void complete(BongoCatUpdateService *service,
    BongoCatUpdateStatus status, const BongoCatUpdateRelease *release,
    const char *error) {
    SDL_LockMutex(service->mutex);
    bool notify = !service->shutting_down;
    if (notify) {
        service->status = status;
        if (release) service->release = *release;
        else memset(&service->release, 0, sizeof(service->release));
        snprintf(service->error, sizeof(service->error), "%s",
            error ? error : "");
    }
    SDL_UnlockMutex(service->mutex);
    if (!notify) return;
    SDL_Event event = {0};
    event.type = service->event_type;
    event.user.data1 = service;
    SDL_PushEvent(&event);
}

static int SDLCALL update_worker(void *userdata) {
    BongoCatUpdateService *service = userdata;
    char *response = NULL;
    char message[256] = {0};
    BongoCatUpdateFetchResult fetched = bongo_cat_update_http_fetch(service,
        &response, message, sizeof(message));
    if (fetched != BONGO_CAT_UPDATE_FETCH_OK) {
        if (fetched != BONGO_CAT_UPDATE_FETCH_CANCELLED)
            complete(service, BONGO_CAT_UPDATE_ERROR, NULL, message);
        return 0;
    }
    BongoCatUpdateRelease release;
    BongoCatError error = {0};
    bool parsed = bongo_cat_update_parse_release(response,
        bongo_cat_update_platform_asset(), &release, &error);
    free(response);
    if (!parsed) {
        complete(service, BONGO_CAT_UPDATE_ERROR, NULL, error.message);
        return 0;
    }
    BongoCatUpdateStatus status = bongo_cat_update_compare_versions(
        release.version, BONGO_CAT_VERSION) > 0
        ? BONGO_CAT_UPDATE_AVAILABLE : BONGO_CAT_UPDATE_CURRENT;
    complete(service, status, &release, NULL);
    return 0;
}

BongoCatUpdateService *bongo_cat_update_create(BongoCatApp *app) {
    if (!app) return NULL;
    BongoCatUpdateService *service = calloc(1, sizeof(*service));
    if (!service) return NULL;
    service->app = app;
    service->mutex = SDL_CreateMutex();
    service->http_mutex = SDL_CreateMutex();
    service->event_type = SDL_RegisterEvents(1);
    if (!service->mutex || !service->http_mutex ||
        service->event_type == (Uint32)-1) {
        if (service->mutex) SDL_DestroyMutex(service->mutex);
        if (service->http_mutex) SDL_DestroyMutex(service->http_mutex);
        free(service);
        return NULL;
    }
    if (!bongo_cat_update_platform_supported())
        service->status = BONGO_CAT_UPDATE_UNSUPPORTED;
    else if (bongo_cat_update_platform_store())
        service->status = BONGO_CAT_UPDATE_STORE;
    else {
        service->status = BONGO_CAT_UPDATE_IDLE;
        service->installed = bongo_cat_update_platform_installed();
    }
    return service;
}

bool bongo_cat_update_check(BongoCatUpdateService *service, bool manual) {
    if (!service) return false;
    reap_worker(service);
    SDL_LockMutex(service->mutex);
    if (service->shutting_down || service->worker ||
        service->status == BONGO_CAT_UPDATE_STORE ||
        service->status == BONGO_CAT_UPDATE_UNSUPPORTED) {
        SDL_UnlockMutex(service->mutex);
        return false;
    }
    service->status = BONGO_CAT_UPDATE_CHECKING;
    service->manual = manual;
    service->error[0] = '\0';
    SDL_LockMutex(service->http_mutex);
    service->http_cancelled = false;
    SDL_UnlockMutex(service->http_mutex);
    service->worker = SDL_CreateThread(update_worker,
        "bongo-cat-update-check", service);
    bool started = service->worker != NULL;
    if (!started) {
        service->status = BONGO_CAT_UPDATE_ERROR;
        snprintf(service->error, sizeof(service->error),
            "Cannot start the update checker");
    }
    SDL_UnlockMutex(service->mutex);
    return started;
}

void bongo_cat_update_start_automatic(BongoCatUpdateService *service) {
    if (!service || !service->app || service->app->smoke ||
        service->app->secondary_pet) return;
    int today = local_day();
    BongoCatSessionState *session = &service->app->session;
    if (today && session->last_update_check_day == today &&
        strcmp(session->last_update_check_version, BONGO_CAT_VERSION) == 0)
        return;
    if (!bongo_cat_update_check(service, false)) return;
    session->last_update_check_day = today;
    snprintf(session->last_update_check_version,
        sizeof(session->last_update_check_version), "%s", BONGO_CAT_VERSION);
}

void bongo_cat_update_snapshot(BongoCatUpdateService *service,
    BongoCatUpdateSnapshot *snapshot) {
    if (!snapshot) return;
    memset(snapshot, 0, sizeof(*snapshot));
    if (!service) {
        snapshot->status = BONGO_CAT_UPDATE_UNSUPPORTED;
        return;
    }
    SDL_LockMutex(service->mutex);
    snapshot->status = service->status;
    snapshot->release = service->release;
    snapshot->installed = service->installed;
    snprintf(snapshot->error, sizeof(snapshot->error), "%s", service->error);
    SDL_UnlockMutex(service->mutex);
}

static const char *tr(BongoCatUpdateService *service, const char *key,
    const char *fallback) {
    return bongo_cat_i18n_get(service->app->i18n, key, fallback);
}

static void show_completion(BongoCatUpdateService *service) {
    BongoCatUpdateSnapshot snapshot;
    bongo_cat_update_snapshot(service, &snapshot);
    char message[192];
    if (snapshot.status == BONGO_CAT_UPDATE_CURRENT) {
        snprintf(message, sizeof(message), "%s v%s", tr(service,
            "native.support.latest", "Already up to date"),
            BONGO_CAT_VERSION);
        bongo_cat_preferences_notice_show(service->app, message, false);
    } else if (snapshot.status == BONGO_CAT_UPDATE_AVAILABLE) {
        snprintf(message, sizeof(message), "%s v%s", tr(service,
            "native.support.updateAvailable", "New version available:"),
            snapshot.release.version);
        bongo_cat_preferences_notice_show(service->app, message, false);
    } else if (snapshot.status == BONGO_CAT_UPDATE_ERROR) {
        bongo_cat_preferences_notice_show(service->app, tr(service,
            "native.support.updateFailed", "Unable to check for updates"),
            true);
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Update check failed: %s", snapshot.error);
    }
}

bool bongo_cat_update_event(BongoCatUpdateService *service,
    const SDL_Event *event) {
    if (!service || !event || event->type != service->event_type ||
        event->user.data1 != service) return false;
    SDL_LockMutex(service->mutex);
    bool manual = service->manual;
    SDL_UnlockMutex(service->mutex);
    reap_worker(service);
    bongo_cat_preferences_invalidate(service->app->preferences);
    if (manual) show_completion(service);
    return true;
}

bool bongo_cat_update_open(BongoCatUpdateService *service) {
    BongoCatUpdateSnapshot snapshot;
    bongo_cat_update_snapshot(service, &snapshot);
    if (snapshot.status == BONGO_CAT_UPDATE_STORE)
        return SDL_OpenURL(STORE_URI) || SDL_OpenURL(STORE_WEB_URL);
    const char *url = RELEASES_URL;
    if (snapshot.status == BONGO_CAT_UPDATE_AVAILABLE) {
        if (snapshot.installed && snapshot.release.installer_url[0])
            url = snapshot.release.installer_url;
        else if (!snapshot.installed && snapshot.release.portable_url[0])
            url = snapshot.release.portable_url;
        else if (snapshot.release.release_url[0])
            url = snapshot.release.release_url;
    }
    return SDL_OpenURL(url);
}

void bongo_cat_update_destroy(BongoCatUpdateService *service) {
    if (!service) return;
    SDL_LockMutex(service->mutex);
    service->shutting_down = true;
    SDL_UnlockMutex(service->mutex);
    bongo_cat_update_http_cancel(service);
    if (service->worker) SDL_WaitThread(service->worker, NULL);
    SDL_Event event;
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, service->event_type,
        service->event_type) > 0) {}
    SDL_DestroyMutex(service->http_mutex);
    SDL_DestroyMutex(service->mutex);
    free(service);
}
