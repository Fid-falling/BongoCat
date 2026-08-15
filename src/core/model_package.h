#ifndef BONGO_CAT_MODEL_PACKAGE_H
#define BONGO_CAT_MODEL_PACKAGE_H

#include "bongo_cat/model.h"

bool bongo_cat_model_package_add(BongoCatModelCatalog *catalog,
    const char *directory, bool preset, bool *handled);

#endif
