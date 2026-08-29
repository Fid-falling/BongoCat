#ifndef BONGO_CAT_PREFERENCES_IMPORT_INTERNAL_H
#define BONGO_CAT_PREFERENCES_IMPORT_INTERNAL_H

#include "preferences_internal.h"

typedef struct BongoCatImportReceipt BongoCatImportReceipt;

typedef struct BongoCatImportJob {
    size_t count;
    char **paths;
    char models_root[BONGO_CAT_PATH_CAP];
    char package_ids[BONGO_CAT_MODEL_CAP][BONGO_CAT_ID_CAP];
    size_t package_id_count;
    size_t resolved_count;
    size_t installed_count;
    size_t restored_count;
    BongoCatResult result;
    BongoCatError error;
    BongoCatImportDialog *dialog;
} BongoCatImportJob;

typedef struct BongoCatImportProgress {
    BongoCatImportJob *job;
    char package_id[BONGO_CAT_ID_CAP];
    size_t installed_count;
} BongoCatImportProgress;

enum {
    BONGO_CAT_IMPORT_EVENT_CODE = 0x42434e49,
    BONGO_CAT_IMPORT_PROGRESS_CODE = 0x42434e4a,
    BONGO_CAT_IMPORT_COMPLETE_CODE = 0x42434e4b
};

struct BongoCatImportDialog {
    SDL_Mutex *mutex;
    Uint32 event_type;
    SDL_WindowID window_id;
    SDL_Thread *worker;
    BongoCatImportJob *worker_job;
    uint64_t started_ns;
    size_t completed;
    size_t total;
    int references;
    bool active;
    bool open;
    bool busy;
};

void bongo_cat_preferences_import_report_progress(BongoCatImportJob *job,
    const BongoCatImportReceipt *receipt, size_t completed);
bool bongo_cat_preferences_import_progress_event(
    BongoCatImportDialog *dialog, BongoCatApp *app, const SDL_Event *event);

#endif
