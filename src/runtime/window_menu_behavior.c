#include "window_menu.h"

#include <string.h>

static const BongoCatBehaviorEntry *nth_behavior(BongoCatApp *app,
    BongoCatBehaviorKind kind, size_t position) {
    if (!app) return NULL;
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatBehaviorEntry *entry = &app->behaviors.entries[i];
        if (entry->kind != kind) continue;
        if (!position) return entry;
        position--;
    }
    return NULL;
}

static const char *behavior_shortcut(const BongoCatApp *app, const char *id) {
    if (!app || !id) return NULL;
    for (size_t i = 0; i < app->config.behavior_shortcut_count; ++i) {
        const BongoCatBehaviorShortcut *binding = &app->config.behavior_shortcuts[i];
        if (strcmp(binding->id, id) == 0) return binding->shortcut;
    }
    return NULL;
}

static bool run_binding(BongoCatApp *app, const BongoCatBehaviorEntry *selected) {
    const char *shortcut = behavior_shortcut(app, selected->id);
    bool handled = bongo_cat_app_run_behavior(app, selected);
    if (!shortcut || !shortcut[0]) return handled;
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatBehaviorEntry *entry = &app->behaviors.entries[i];
        const char *bound = behavior_shortcut(app, entry->id);
        if (entry != selected && bound && strcmp(bound, shortcut) == 0)
            handled = bongo_cat_app_run_behavior(app, entry) || handled;
    }
    return handled;
}

void bongo_cat_window_behavior_labels(BongoCatApp *app,
    const char **motions, size_t *motion_count,
    const char **expressions, size_t *expression_count) {
    if (!motion_count || !expression_count) return;
    *motion_count = 0; *expression_count = 0;
    if (!app || !app->config.model.behavior) return;
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatBehaviorEntry *entry = &app->behaviors.entries[i];
        if (entry->kind == BONGO_CAT_BEHAVIOR_MOTION && motions)
            motions[(*motion_count)++] = entry->label;
        else if (entry->kind == BONGO_CAT_BEHAVIOR_EXPRESSION && expressions)
            expressions[(*expression_count)++] = entry->label;
    }
}

bool bongo_cat_window_behavior_action(BongoCatApp *app,
    BongoCatMenuAction action) {
    BongoCatBehaviorKind kind;
    size_t position;
    if (action >= BONGO_CAT_MENU_MOTION_FIRST &&
        action < BONGO_CAT_MENU_MOTION_FIRST + BONGO_CAT_BEHAVIOR_CAP) {
        kind = BONGO_CAT_BEHAVIOR_MOTION;
        position = (size_t)(action - BONGO_CAT_MENU_MOTION_FIRST);
    } else if (action >= BONGO_CAT_MENU_EXPRESSION_FIRST &&
        action < BONGO_CAT_MENU_EXPRESSION_FIRST + BONGO_CAT_BEHAVIOR_CAP) {
        kind = BONGO_CAT_BEHAVIOR_EXPRESSION;
        position = (size_t)(action - BONGO_CAT_MENU_EXPRESSION_FIRST);
    } else return false;
    const BongoCatBehaviorEntry *entry = nth_behavior(app, kind, position);
    return entry && run_binding(app, entry);
}

bool bongo_cat_window_behavior_menu_action(BongoCatMenuAction action) {
    return (action >= BONGO_CAT_MENU_MOTION_FIRST &&
        action < BONGO_CAT_MENU_MOTION_FIRST + BONGO_CAT_BEHAVIOR_CAP) ||
        (action >= BONGO_CAT_MENU_EXPRESSION_FIRST &&
            action < BONGO_CAT_MENU_EXPRESSION_FIRST + BONGO_CAT_BEHAVIOR_CAP);
}

bool bongo_cat_window_behavior_self_test(BongoCatApp *app) {
    if (!app) return false;
    bool enabled = app->config.model.behavior, passed = true;
    app->config.model.behavior = true;
    if (nth_behavior(app, BONGO_CAT_BEHAVIOR_MOTION, 0))
        passed = bongo_cat_window_behavior_action(app,
            BONGO_CAT_MENU_MOTION_FIRST) && passed;
    if (nth_behavior(app, BONGO_CAT_BEHAVIOR_EXPRESSION, 0))
        passed = bongo_cat_window_behavior_action(app,
            BONGO_CAT_MENU_EXPRESSION_FIRST) && passed;
    app->config.model.behavior = enabled;
    return passed;
}
