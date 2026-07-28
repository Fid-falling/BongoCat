#include "preferences_internal.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

typedef struct BongoCatNeoImportJob {
    size_t count;
    char **paths;
} BongoCatNeoImportJob;

enum { BONGO_CAT_NEO_IMPORT_EVENT_CODE = 0x42434e49 };

struct BongoCatNeoImportDialog {
    SDL_Mutex *mutex;
    Uint32 event_type;
    SDL_WindowID window_id;
    int references;
    bool active;
    bool open;
};

static void free_job(BongoCatNeoImportJob *job) {
    if (!job) return;
    for (size_t i = 0; i < job->count; ++i) SDL_free(job->paths[i]);
    SDL_free(job->paths);
    SDL_free(job);
}

static void release_dialog(BongoCatNeoImportDialog *dialog) {
    bool destroy = false;
    SDL_LockMutex(dialog->mutex);
    destroy = --dialog->references == 0;
    SDL_UnlockMutex(dialog->mutex);
    if (destroy) {
        SDL_DestroyMutex(dialog->mutex);
        SDL_free(dialog);
    }
}

BongoCatNeoImportDialog *bongo_cat_neo_preferences_import_create(void) {
    BongoCatNeoImportDialog *dialog = SDL_calloc(1, sizeof(*dialog));
    if (!dialog) return NULL;
    dialog->event_type = SDL_RegisterEvents(1);
    dialog->mutex = SDL_CreateMutex();
    if (dialog->event_type == (Uint32)-1 || !dialog->mutex) {
        if (dialog->mutex) SDL_DestroyMutex(dialog->mutex);
        SDL_free(dialog);
        return NULL;
    }
    dialog->references = 1;
    dialog->active = true;
    return dialog;
}

void bongo_cat_neo_preferences_import_destroy(BongoCatNeoImportDialog *dialog) {
    if (!dialog) return;
    SDL_LockMutex(dialog->mutex);
    dialog->active = false;
    dialog->open = false;
    SDL_Event event;
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, dialog->event_type,
        dialog->event_type) > 0)
        if (event.user.code == BONGO_CAT_NEO_IMPORT_EVENT_CODE &&
            event.user.data2 == dialog)
            free_job((BongoCatNeoImportJob *)event.user.data1);
    SDL_UnlockMutex(dialog->mutex);
    release_dialog(dialog);
}

bool bongo_cat_neo_preferences_import_is_open(const BongoCatNeoImportDialog *dialog) {
    if (!dialog) return false;
    SDL_LockMutex(dialog->mutex);
    bool open = dialog->open;
    SDL_UnlockMutex(dialog->mutex);
    return open;
}

static BongoCatNeoImportJob *copy_job(const char *const *files) {
    if (!files || !files[0]) return NULL;
    size_t count = 0;
    while (files[count]) ++count;
    BongoCatNeoImportJob *job = SDL_calloc(1, sizeof(*job));
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

static void SDLCALL import_callback(void *userdata, const char *const *files,
    int filter) {
    (void)filter;
    BongoCatNeoImportDialog *dialog = userdata;
    BongoCatNeoImportJob *job = copy_job(files);
    SDL_Event event = {0};
    bool pushed = false;
    SDL_LockMutex(dialog->mutex);
    event.type = dialog->event_type;
    event.user.windowID = dialog->window_id;
    event.user.code = BONGO_CAT_NEO_IMPORT_EVENT_CODE;
    event.user.data1 = job;
    event.user.data2 = dialog;
    if (dialog->active) pushed = SDL_PushEvent(&event);
    if (!pushed) dialog->open = false;
    SDL_UnlockMutex(dialog->mutex);
    if (!pushed) free_job(job);
    release_dialog(dialog);
}

bool bongo_cat_neo_preferences_import_open(BongoCatNeoImportDialog *dialog,
    SDL_Window *window) {
    if (!dialog || !window) return false;
    SDL_LockMutex(dialog->mutex);
    if (!dialog->active || dialog->open) {
        SDL_UnlockMutex(dialog->mutex);
        return false;
    }
    dialog->open = true;
    dialog->window_id = SDL_GetWindowID(window);
    ++dialog->references;
    SDL_UnlockMutex(dialog->mutex);
    SDL_ShowOpenFolderDialog(import_callback, dialog, window, NULL, true);
    return true;
}

bool bongo_cat_neo_preferences_import_event(BongoCatNeoImportDialog *dialog,
    BongoCatNeoApp *app, SDL_Window *window, const SDL_Event *event) {
    if (!dialog || !event || event->type != dialog->event_type ||
        event->user.code != BONGO_CAT_NEO_IMPORT_EVENT_CODE ||
        event->user.data2 != dialog) return false;
    BongoCatNeoImportJob *job = (BongoCatNeoImportJob *)event->user.data1;
    SDL_WindowID current = window ? SDL_GetWindowID(window) : 0;
    SDL_LockMutex(dialog->mutex);
    dialog->open = false;
    bool accept = dialog->active && current && current == event->user.windowID;
    SDL_UnlockMutex(dialog->mutex);
    if (accept && app && window && job) {
        SDL_GL_MakeCurrent(app->window, app->gl_context);
        for (size_t i = 0; i < job->count; ++i)
            bongo_cat_neo_preferences_import_path(app, window, job->paths[i]);
    }
    free_job(job);
    return true;
}
