#include "preferences_import_internal.h"
#include "model_import.h"

#include <SDL3/SDL.h>
#include <stdio.h>

void bongo_cat_preferences_import_report_progress(BongoCatImportJob *job,
    const BongoCatImportReceipt *receipt, size_t completed) {
    BongoCatImportProgress *progress = receipt && receipt->count
        ? SDL_calloc(1, sizeof(*progress)) : NULL;
    if (progress) {
        progress->job = job;
        progress->installed_count = receipt->installed_count;
        bongo_cat_import_package_base_id(progress->package_id,
            sizeof(progress->package_id), receipt->ids[0]);
    }
    SDL_Event event = {0};
    SDL_LockMutex(job->dialog->mutex);
    bool current = job->dialog->active &&
        job->dialog->worker_job == job;
    if (current) {
        job->dialog->completed = completed;
        if (job->dialog->total < completed)
            job->dialog->total = completed;
    }
    bool pushed = false;
    if (current && progress) {
        event.type = job->dialog->event_type;
        event.user.windowID = job->dialog->window_id;
        event.user.code = BONGO_CAT_IMPORT_PROGRESS_CODE;
        event.user.data1 = progress;
        event.user.data2 = job->dialog;
        pushed = SDL_PushEvent(&event);
    }
    SDL_UnlockMutex(job->dialog->mutex);
    if (!pushed) SDL_free(progress);
}

bool bongo_cat_preferences_import_progress_event(
    BongoCatImportDialog *dialog, BongoCatApp *app, const SDL_Event *event) {
    if (!dialog || !event ||
        event->user.code != BONGO_CAT_IMPORT_PROGRESS_CODE) return false;
    BongoCatImportProgress *progress =
        (BongoCatImportProgress *)event->user.data1;
    SDL_LockMutex(dialog->mutex);
    bool current = progress && dialog->active &&
        dialog->worker_job == progress->job;
    SDL_UnlockMutex(dialog->mutex);
    if (current && app) {
        bool restored = bongo_cat_settings_restore_model_package(
            &app->settings, progress->package_id);
        if (restored) progress->job->restored_count++;
        if (progress->installed_count || restored)
            bongo_cat_app_request_model_package_refresh(app,
                progress->package_id);
    }
    SDL_free(progress);
    return true;
}
