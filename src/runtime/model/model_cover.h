#ifndef BONGO_CAT_MODEL_COVER_H
#define BONGO_CAT_MODEL_COVER_H

#include "bongo_cat/app.h"
#include "model_cover_paths.h"

void bongo_cat_model_cover_schedule(BongoCatApp *app,
    const BongoCatModelEntry *entry);
bool bongo_cat_model_cover_pending(const BongoCatApp *app);
const char *bongo_cat_model_cover_pending_path(const BongoCatApp *app);
void bongo_cat_model_cover_capture_before_switch(BongoCatApp *app);
void bongo_cat_model_cover_capture(BongoCatApp *app, int width, int height);

#endif
