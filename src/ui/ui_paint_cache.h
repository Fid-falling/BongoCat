#ifndef BONGO_CAT_NEO_UI_PAINT_CACHE_H
#define BONGO_CAT_NEO_UI_PAINT_CACHE_H

#include "ui_backend.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum BongoCatNeoUIPaintKind {
    BONGO_CAT_NEO_UI_PAINT_GRADIENT = 1,
    BONGO_CAT_NEO_UI_PAINT_RADIAL = 2,
    BONGO_CAT_NEO_UI_PAINT_SHADOW = 3,
    BONGO_CAT_NEO_UI_PAINT_RADIAL_CIRCLE = 4,
    BONGO_CAT_NEO_UI_PAINT_DASHED_ROUNDED = 5
} BongoCatNeoUIPaintKind;

typedef struct BongoCatNeoUIPaintKey {
    int kind, width, height, radius;
    int first_parameter, second_parameter;
    uint32_t first_color, second_color;
} BongoCatNeoUIPaintKey;

typedef struct BongoCatNeoUIPaintTexture BongoCatNeoUIPaintTexture;

BongoCatNeoUIPaintTexture *bongo_cat_neo_ui_paint_cache_get(
    BongoCatNeoUIBackend *backend, const BongoCatNeoUIPaintKey *key);
bool bongo_cat_neo_ui_paint_cache_ready(
    const BongoCatNeoUIPaintTexture *item);
bool bongo_cat_neo_ui_paint_cache_upload(BongoCatNeoUIPaintTexture *item,
    const unsigned char *pixels, bool single_channel);
void bongo_cat_neo_ui_paint_cache_draw(struct nk_context *context,
    struct nk_rect bounds, const BongoCatNeoUIPaintTexture *item,
    struct nk_color tint);
void bongo_cat_neo_ui_paint_cache_destroy(BongoCatNeoUIBackend *backend);

#endif
