#ifndef BONGO_CAT_MODEL_IMPORT_TAURI_MVER_INTERNAL_H
#define BONGO_CAT_MODEL_IMPORT_TAURI_MVER_INTERNAL_H

#include "model_import.h"
#include "bongo_cat/path.h"

#define TAURI_KEY_CAP 256

typedef struct TauriKeyFile {
    char name[BONGO_CAT_ID_CAP];
    char path[BONGO_CAT_PATH_CAP];
    int code;
} TauriKeyFile;

typedef struct TauriKeyFiles {
    TauriKeyFile values[TAURI_KEY_CAP];
    size_t count;
} TauriKeyFiles;

BongoCatPathVisit bongo_cat_tauri_collect_keys(void *userdata,
    const char *dirname, const char *name);
int bongo_cat_tauri_compare_keys(const void *left, const void *right);
bool bongo_cat_tauri_copy_model_tree(const char *source, const char *target,
    unsigned depth, BongoCatError *error);

#endif
