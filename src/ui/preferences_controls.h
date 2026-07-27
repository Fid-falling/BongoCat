#ifndef BONGO_CAT_NEO_PREFERENCES_CONTROLS_H
#define BONGO_CAT_NEO_PREFERENCES_CONTROLS_H

#include <stdbool.h>
#include "nuklear_config.h"

bool bongo_cat_neo_pref_control_float(struct nk_context *context, const char *id,
    float minimum, float *value, float maximum, float step);
bool bongo_cat_neo_pref_control_int(struct nk_context *context, const char *id,
    int minimum, int *value, int maximum, int step);
bool bongo_cat_neo_pref_control_slider(struct nk_context *context, const char *id,
    float minimum, float *value, float maximum, float step);
bool bongo_cat_neo_pref_control_toggle(struct nk_context *context,
    const char *id, bool *value);
int bongo_cat_neo_pref_control_combo(struct nk_context *context, const char *id,
    const char *const *items, int count, int selected);
bool bongo_cat_neo_pref_controls_animating(struct nk_context *context);

#endif
