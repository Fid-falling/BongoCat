#include "preferences_import_internal.h"
#include "preferences_state.h"
#include "model_import.h"
#ifdef _WIN32
#include "windows_dialog.h"
#endif
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void free_job(BongoCatImportJob *job) {
    if (!job) return;
    for (size_t i = 0; i < job->count; ++i) SDL_free(job->paths[i]);
    SDL_free(job->paths);
    SDL_free(job);
}
static void release_dialog(BongoCatImportDialog *dialog) {
    bool destroy = false;
    SDL_LockMutex(dialog->mutex);
    destroy = --dialog->references == 0;
    SDL_UnlockMutex(dialog->mutex);
    if (destroy) {
        SDL_DestroyMutex(dialog->mutex);
        SDL_free(dialog);
    }
}
void bongo_cat_preferences_import_destroy(BongoCatImportDialog *dialog) {
    if (!dialog) return;
    SDL_LockMutex(dialog->mutex);
    dialog->active = false;
    dialog->open = false;
    SDL_Thread *worker = dialog->worker;
    SDL_UnlockMutex(dialog->mutex);
    if (worker) SDL_WaitThread(worker, NULL);
    bool release_worker = false;
    SDL_LockMutex(dialog->mutex);
    dialog->worker = NULL;
    SDL_Event event;
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, dialog->event_type,
        dialog->event_type) > 0) {
        if (event.user.data2 != dialog) continue;
        BongoCatImportJob *job = (BongoCatImportJob *)event.user.data1;
        release_worker = release_worker || (job && job->completion);
        free_job(job);
    }
    dialog->worker_job = NULL;
    dialog->busy = false;
    dialog->started_ns = 0;
    dialog->completed = dialog->total = 0;
    SDL_UnlockMutex(dialog->mutex);
    if (release_worker) release_dialog(dialog);
    release_dialog(dialog);
}
static BongoCatImportJob *copy_job(const char *const *files) {
    if (!files || !files[0]) return NULL;
    size_t count = 0;
    while (files[count]) ++count;
    BongoCatImportJob *job = SDL_calloc(1, sizeof(*job));
    if (!job) return NULL;
    job->paths = SDL_calloc(count, sizeof(*job->paths));
    if (!job->paths) { SDL_free(job); return NULL; }
    job->count = count;
    for (size_t i = 0; i < count; ++i) {
        job->paths[i] = SDL_strdup(files[i]);
        if (!job->paths[i]) { free_job(job); return NULL; }
    }
    return job;
}

static BongoCatImportJob *copy_path(const char *path) {
    const char *files[] = {path, NULL};
    return path && path[0] ? copy_job(files) : NULL;
}

static void remember_package(BongoCatImportJob *job, const char *id) {
    if (!job || !id || !id[0]) return;
    for (size_t i = 0; i < job->package_id_count; ++i)
        if (!strcmp(job->package_ids[i], id)) return;
    if (job->package_id_count >= BONGO_CAT_MODEL_CAP) return;
    snprintf(job->package_ids[job->package_id_count++], BONGO_CAT_ID_CAP,
        "%s", id);
}

static int SDLCALL import_worker(void *userdata) {
    BongoCatImportJob *job = userdata;
    if (!SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW)) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
            "Cannot lower model import thread priority: %s", SDL_GetError());
        SDL_ClearError();
    }
    job->result = BONGO_CAT_ERROR_FORMAT;
    BongoCatImportSession *session = bongo_cat_import_session_create(
        job->models_root, &job->error);
    if (!session) job->result = job->error.code ? job->error.code :
        BONGO_CAT_ERROR_IO;
    for (size_t i = 0; session && i < job->count; ++i) {
        BongoCatImportReceipt receipt;
        BongoCatError error = {0};
        BongoCatResult result = bongo_cat_import_session_install(session,
            job->paths[i], &receipt, &error);
        if (result == BONGO_CAT_OK) {
            if (receipt.count) remember_package(job, receipt.ids[0]);
            for (size_t j = 0; j < receipt.count; ++j) {
                if (!job->first_id[0] ||
                    (receipt.installed[j] && !job->first_id_installed)) {
                    snprintf(job->first_id, sizeof(job->first_id), "%s",
                        receipt.ids[j]);
                    job->first_id_installed = receipt.installed[j];
                }
            }
            job->resolved_count += receipt.count;
            job->installed_count += receipt.installed_count;
            job->result = BONGO_CAT_OK;
        } else if (!job->error.message[0]) {
            job->result = result;
            job->error = error;
        }
        SDL_LockMutex(job->dialog->mutex);
        if (job->dialog->worker_job == job)
            job->dialog->completed = i + 1;
        SDL_UnlockMutex(job->dialog->mutex);
    }
    bongo_cat_import_session_destroy(session);
    if (job->resolved_count) job->result = BONGO_CAT_OK;
    if (!job->resolved_count && !job->error.message[0])
        bongo_cat_error_set(&job->error, BONGO_CAT_ERROR_FORMAT,
            "No model could be imported");
    SDL_Event event = {0};
    SDL_LockMutex(job->dialog->mutex);
    bool notify = job->dialog->active;
    if (notify) {
        job->completion = true;
        event.type = job->dialog->event_type;
        event.user.windowID = job->dialog->window_id;
        event.user.code = BONGO_CAT_IMPORT_COMPLETE_CODE;
        event.user.data1 = job;
        event.user.data2 = job->dialog;
        notify = SDL_PushEvent(&event);
    }
    if (!notify) {
        job->dialog->worker_job = NULL;
        job->dialog->busy = false;
        job->dialog->started_ns = 0;
        job->dialog->completed = job->dialog->total = 0;
    }
    SDL_UnlockMutex(job->dialog->mutex);
    if (!notify) {
        BongoCatImportDialog *dialog = job->dialog;
        free_job(job);
        release_dialog(dialog);
    }
    return 0;
}

static bool start_job(BongoCatImportDialog *dialog, BongoCatApp *app,
    SDL_WindowID window_id, BongoCatImportJob *job) {
    if (!dialog || !app || !job || !app->models_root[0]) return false;
    snprintf(job->models_root, sizeof(job->models_root), "%s", app->models_root);
    job->dialog = dialog;
    SDL_LockMutex(dialog->mutex);
    if (!dialog->active || dialog->busy) {
        SDL_UnlockMutex(dialog->mutex);
        return false;
    }
    dialog->window_id = window_id;
    dialog->busy = true;
    dialog->worker_job = job;
    dialog->started_ns = SDL_GetTicksNS();
    dialog->completed = 0;
    dialog->total = job->count;
    ++dialog->references;
    dialog->worker = SDL_CreateThread(import_worker,
        BONGO_CAT_SLUG "-model-import", job);
    if (!dialog->worker) {
        dialog->worker_job = NULL;
        dialog->busy = false;
        dialog->started_ns = 0;
        dialog->completed = dialog->total = 0;
        --dialog->references;
    }
    SDL_UnlockMutex(dialog->mutex);
    return dialog->worker != NULL;
}

static void SDLCALL import_callback(void *userdata, const char *const *files,
    int filter) {
    (void)filter;
    BongoCatImportDialog *dialog = userdata;
    BongoCatImportJob *job = copy_job(files);
    SDL_Event event = {0};
    bool pushed = false;
    SDL_LockMutex(dialog->mutex);
    event.type = dialog->event_type;
    event.user.windowID = dialog->window_id;
    event.user.code = BONGO_CAT_IMPORT_EVENT_CODE;
    event.user.data1 = job;
    event.user.data2 = dialog;
    if (dialog->active && !dialog->busy && job) pushed = SDL_PushEvent(&event);
    if (!pushed) dialog->open = false;
    SDL_UnlockMutex(dialog->mutex);
    if (!pushed) free_job(job);
    release_dialog(dialog);
}

bool bongo_cat_preferences_import_open(BongoCatImportDialog *dialog,
    SDL_Window *window) {
    if (!dialog || !window) return false;
    SDL_LockMutex(dialog->mutex);
    if (!dialog->active || dialog->open || dialog->busy) {
        SDL_UnlockMutex(dialog->mutex);
        return false;
    }
    dialog->open = true;
    dialog->window_id = SDL_GetWindowID(window);
    ++dialog->references;
    SDL_UnlockMutex(dialog->mutex);
#ifdef _WIN32
    bongo_cat_windows_show_open_folder_dialog(import_callback, dialog, window,
        NULL, true);
#else
    SDL_ShowOpenFolderDialog(import_callback, dialog, window, NULL, true);
#endif
    return true;
}

static void complete_job(BongoCatImportDialog *dialog, BongoCatApp *app,
    BongoCatImportJob *job) {
    SDL_Thread *worker = NULL;
    SDL_LockMutex(dialog->mutex);
    if (dialog->worker_job == job) {
        worker = dialog->worker;
        dialog->worker = NULL;
        dialog->worker_job = NULL;
        dialog->busy = false;
        dialog->started_ns = 0;
        dialog->completed = dialog->total = 0;
    }
    SDL_UnlockMutex(dialog->mutex);
    if (worker) SDL_WaitThread(worker, NULL);
    char restored_id[BONGO_CAT_ID_CAP] = {0};
    size_t restored_count = 0;
    if (job->result == BONGO_CAT_OK)
        for (size_t i = 0; i < job->package_id_count; ++i)
            if (bongo_cat_settings_restore_model_package(&app->settings,
                    job->package_ids[i])) {
                restored_count++;
                if (!restored_id[0])
                    snprintf(restored_id, sizeof(restored_id), "%s",
                        job->package_ids[i]);
            }
    bool catalog_changed = job->installed_count > 0 || restored_count > 0;
    if (job->result == BONGO_CAT_OK && job->first_id[0] && catalog_changed) {
        SDL_GL_MakeCurrent(app->window, app->gl_context);
        bongo_cat_app_refresh_installed_models(app);
        const char *selected = job->first_id_installed || !restored_id[0]
            ? job->first_id : restored_id;
        if (!bongo_cat_app_select_model(app, selected)) {
            job->result = BONGO_CAT_ERROR_CUBISM;
            bongo_cat_error_set(&job->error, job->result,
                "Model imported but could not be loaded");
        }
    }
    bongo_cat_preferences_import_complete(app, job->result, &job->error,
        job->resolved_count, job->installed_count + restored_count);
    free_job(job);
    release_dialog(dialog);
}

bool bongo_cat_preferences_import_event(BongoCatImportDialog *dialog,
    BongoCatApp *app, const SDL_Event *event) {
    if (!dialog || !event || event->type != dialog->event_type ||
        event->user.data2 != dialog) return false;
    BongoCatImportJob *job = (BongoCatImportJob *)event->user.data1;
    if (event->user.code == BONGO_CAT_IMPORT_EVENT_CODE) {
        SDL_Window *owner = SDL_GetWindowFromID(event->user.windowID);
        SDL_LockMutex(dialog->mutex);
        dialog->open = false;
        bool accept = dialog->active && owner != NULL;
        SDL_UnlockMutex(dialog->mutex);
        if (accept && app && job && !start_job(dialog, app,
            event->user.windowID, job)) free_job(job);
        else if (!accept) free_job(job);
        return true;
    }
    if (event->user.code == BONGO_CAT_IMPORT_COMPLETE_CODE && app && job) {
        complete_job(dialog, app, job);
        return true;
    }
    return false;
}

void bongo_cat_preferences_import_path(BongoCatApp *app,
    SDL_Window *window, const char *path) {
    if (!app || !app->preferences || !window || !path || !path[0]) return;
    BongoCatImportJob *job = copy_path(path);
    if (!job || !start_job(app->preferences->import_dialog, app,
        SDL_GetWindowID(window), job)) free_job(job);
    else app->preferences->render_dirty = true;
}
