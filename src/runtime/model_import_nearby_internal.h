#ifndef BONGO_CAT_MODEL_IMPORT_NEARBY_INTERNAL_H
#define BONGO_CAT_MODEL_IMPORT_NEARBY_INTERNAL_H

#include "model_import.h"

#define BONGO_CAT_NEARBY_CACHE_MARKER ".bongo-cat-cache.json"
#define BONGO_CAT_NEARBY_CACHE_SCHEMA 1

bool bongo_cat_nearby_identity(const BongoCatImportCandidate *candidate,
    char output[65], BongoCatError *error);
bool bongo_cat_nearby_signature(const BongoCatImportCandidate *candidate,
    char output[65], BongoCatError *error);
#endif
