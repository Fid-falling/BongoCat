#ifndef BONGO_CAT_NEO_OVERLAY_H
#define BONGO_CAT_NEO_OVERLAY_H

#include "bongo_cat_neo/common.h"

typedef struct BongoCatNeoOverlay BongoCatNeoOverlay;

BongoCatNeoOverlay *bongo_cat_neo_overlay_create(BongoCatNeoError *error);
void bongo_cat_neo_overlay_destroy(BongoCatNeoOverlay *overlay);
BongoCatNeoResult bongo_cat_neo_overlay_load(BongoCatNeoOverlay *overlay, const char *model_directory,
    BongoCatNeoError *error);
int bongo_cat_neo_overlay_key(BongoCatNeoOverlay *overlay, const char *name, bool pressed);
bool bongo_cat_neo_overlay_effect(BongoCatNeoOverlay *overlay, const char *path);
bool bongo_cat_neo_overlay_hand_active(const BongoCatNeoOverlay *overlay, bool right);
void bongo_cat_neo_overlay_draw_background(BongoCatNeoOverlay *overlay, bool mirror);
void bongo_cat_neo_overlay_draw_keys(BongoCatNeoOverlay *overlay, bool mirror);
void bongo_cat_neo_overlay_draw_effect(BongoCatNeoOverlay *overlay, bool mirror);

#endif
