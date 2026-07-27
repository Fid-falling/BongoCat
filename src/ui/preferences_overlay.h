#ifndef BONGO_CAT_NEO_PREFERENCES_OVERLAY_H
#define BONGO_CAT_NEO_PREFERENCES_OVERLAY_H

#include "ui_catime.h"

#include <stdint.h>

typedef struct BongoCatNeoOverlayFrame {
    struct nk_rect panel;
    float visibility;
    float close_amount;
    bool finished;
} BongoCatNeoOverlayFrame;

BongoCatNeoOverlayFrame bongo_cat_neo_preferences_overlay_frame(
    struct nk_rect region, float width, float height, uint64_t opened_ns,
    uint64_t closing_ns);
void bongo_cat_neo_preferences_overlay_draw(struct nk_context *context,
    struct nk_rect region, const BongoCatNeoOverlayFrame *frame,
    BongoCatNeoUIPalette palette);
struct nk_color bongo_cat_neo_preferences_overlay_alpha(
    struct nk_color color, float visibility);

#endif
