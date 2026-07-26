#ifndef BONGO_CAT_NEO_UI_ANIMATION_H
#define BONGO_CAT_NEO_UI_ANIMATION_H

#include "nuklear_config.h"
#include <stdbool.h>

float bongo_cat_neo_ui_animate(struct nk_context *context, const char *id,
    float target, float duration_ms);
bool bongo_cat_neo_ui_animations_active(const struct nk_context *context);
void bongo_cat_neo_ui_animations_reset(const struct nk_context *context);

#endif
