#ifndef BONGO_CAT_NEO_WINDOW_MENU_INTERNAL_H
#define BONGO_CAT_NEO_WINDOW_MENU_INTERNAL_H

#include "runtime.h"

typedef struct BongoCatNeoWindowMenuPreview {
    BongoCatNeoApp *app;
    char model[BONGO_CAT_NEO_ID_CAP];
    float scale, opacity;
    BongoCatNeoMenuAction last;
    uint64_t last_tick_ns;
} BongoCatNeoWindowMenuPreview;

void bongo_cat_neo_window_menu_preview_init(BongoCatNeoWindowMenuPreview *state,
    BongoCatNeoApp *app);
void bongo_cat_neo_window_menu_preview(void *userdata, BongoCatNeoMenuAction action);
void bongo_cat_neo_window_menu_preview_tick(void *userdata);
void bongo_cat_neo_window_menu_restore(void *userdata, BongoCatNeoMenuAction selected);
void bongo_cat_neo_window_behavior_labels(BongoCatNeoApp *app,
    const char **motions, size_t *motion_count,
    const char **expressions, size_t *expression_count);
bool bongo_cat_neo_window_behavior_menu_action(BongoCatNeoMenuAction action);
bool bongo_cat_neo_window_behavior_action(BongoCatNeoApp *app,
    BongoCatNeoMenuAction action);
bool bongo_cat_neo_window_behavior_self_test(BongoCatNeoApp *app);

#endif
