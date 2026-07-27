#ifndef BONGO_CAT_NEO_PREFERENCES_WIDGETS_H
#define BONGO_CAT_NEO_PREFERENCES_WIDGETS_H

#include <stdbool.h>
#include "nuklear_config.h"

void bongo_cat_neo_pref_section(struct nk_context *context, const char *title);
bool bongo_cat_neo_pref_toggle(struct nk_context *context, const char *id,
    const char *title, const char *description, bool *value);
bool bongo_cat_neo_pref_float(struct nk_context *context, const char *id,
    const char *title, const char *description, float minimum, float *value,
    float maximum, float step);
bool bongo_cat_neo_pref_int(struct nk_context *context, const char *id,
    const char *title, const char *description, int minimum, int *value,
    int maximum, int step);
bool bongo_cat_neo_pref_slider(struct nk_context *context, const char *id,
    const char *title, const char *description, float minimum, float *value,
    float maximum, float step);
int bongo_cat_neo_pref_combo(struct nk_context *context, const char *id,
    const char *title, const char *description, const char *const *items,
    int count, int selected);
int bongo_cat_neo_pref_edit(struct nk_context *context, const char *id,
    const char *title, const char *description, const char *value,
    bool recording, const char *idle_hint, const char *record_hint);
bool bongo_cat_neo_pref_button(struct nk_context *context, const char *id,
    const char *title, const char *description, const char *button);
void bongo_cat_neo_pref_status(struct nk_context *context, const char *id,
    const char *title, const char *description);

#endif
