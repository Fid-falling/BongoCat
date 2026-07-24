#ifndef BONGO_CAT_NEO_MODEL_STORAGE_H
#define BONGO_CAT_NEO_MODEL_STORAGE_H

#include "bongo_cat_neo/common.h"

bool bongo_cat_neo_model_remove_tree(const char *path, BongoCatNeoError *error);
bool bongo_cat_neo_model_cleanup_imports(const char *root, BongoCatNeoError *error);

#endif
