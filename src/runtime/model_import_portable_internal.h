#ifndef BONGO_CAT_MODEL_IMPORT_PORTABLE_INTERNAL_H
#define BONGO_CAT_MODEL_IMPORT_PORTABLE_INTERNAL_H

#include "model_import.h"

#define BONGO_CAT_PORTABLE_MARKER ".bongo-cat-portable.json"
#define BONGO_CAT_PORTABLE_SCHEMA_VERSION 10

bool bongo_cat_portable_identity(const BongoCatImportCandidate *candidate,
    char output[65], BongoCatError *error);
bool bongo_cat_portable_signature(const BongoCatImportCandidate *candidate,
    char output[65], BongoCatError *error);
void bongo_cat_portable_migrate_config(BongoCatApp *app,
    const char *cache_root);

#endif
