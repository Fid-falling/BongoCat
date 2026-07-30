#ifndef BONGO_CAT_WINDOW_MENU_INTERNAL_H
#define BONGO_CAT_WINDOW_MENU_INTERNAL_H

#include "runtime.h"

typedef struct BongoCatWindowMenuPreview {
    BongoCatApp *app;
    char model[BONGO_CAT_ID_CAP];
    float scale, opacity;
    BongoCatMenuAction last;
    uint64_t last_tick_ns;
} BongoCatWindowMenuPreview;

void bongo_cat_window_menu_preview_init(BongoCatWindowMenuPreview *state,
    BongoCatApp *app);
void bongo_cat_window_menu_preview(void *userdata, BongoCatMenuAction action);
void bongo_cat_window_menu_preview_tick(void *userdata);
void bongo_cat_window_menu_restore(void *userdata, BongoCatMenuAction selected);
void bongo_cat_window_behavior_labels(BongoCatApp *app,
    const char **motions, size_t *motion_count,
    const char **expressions, size_t *expression_count);
bool bongo_cat_window_behavior_menu_action(BongoCatMenuAction action);
bool bongo_cat_window_behavior_action(BongoCatApp *app,
    BongoCatMenuAction action);
bool bongo_cat_window_behavior_self_test(BongoCatApp *app);

#endif
