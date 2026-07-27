#ifndef BONGO_CAT_NEO_PREFERENCES_MODEL_COVER_H
#define BONGO_CAT_NEO_PREFERENCES_MODEL_COVER_H

#include "bongo_cat_neo/model.h"

typedef struct BongoCatNeoApp BongoCatNeoApp;

typedef struct BongoCatNeoModelCover {
    unsigned int texture;
    int width;
    int height;
} BongoCatNeoModelCover;

void bongo_cat_neo_preferences_model_covers_begin(void);
const BongoCatNeoModelCover *bongo_cat_neo_preferences_model_cover(
    BongoCatNeoApp *app, const BongoCatNeoModelEntry *entry,
    int pixel_width, int pixel_height);
void bongo_cat_neo_preferences_model_covers_prune(BongoCatNeoApp *app);

#endif
