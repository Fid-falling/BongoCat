#include "runtime.h"

#include <string.h>

static const BongoCatNeoBehaviorEntry *nth_behavior(BongoCatNeoApp *app,
    BongoCatNeoBehaviorKind kind, size_t position) {
    if (!app) return NULL;
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatNeoBehaviorEntry *entry = &app->behaviors.entries[i];
        if (entry->kind != kind) continue;
        if (!position) return entry;
        position--;
    }
    return NULL;
}

static const char *behavior_shortcut(const BongoCatNeoApp *app, const char *id) {
    if (!app || !id) return NULL;
    for (size_t i = 0; i < app->config.behavior_shortcut_count; ++i) {
        const BongoCatNeoBehaviorShortcut *binding = &app->config.behavior_shortcuts[i];
        if (strcmp(binding->id, id) == 0) return binding->shortcut;
    }
    return NULL;
}

static bool run_binding(BongoCatNeoApp *app, const BongoCatNeoBehaviorEntry *selected) {
    const char *shortcut = behavior_shortcut(app, selected->id);
    bool handled = bongo_cat_neo_app_run_behavior(app, selected);
    if (!shortcut || !shortcut[0]) return handled;
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatNeoBehaviorEntry *entry = &app->behaviors.entries[i];
        const char *bound = behavior_shortcut(app, entry->id);
        if (entry != selected && bound && strcmp(bound, shortcut) == 0)
            handled = bongo_cat_neo_app_run_behavior(app, entry) || handled;
    }
    return handled;
}

void bongo_cat_neo_window_behavior_labels(BongoCatNeoApp *app,
    const char **motions, size_t *motion_count,
    const char **expressions, size_t *expression_count) {
    if (!motion_count || !expression_count) return;
    *motion_count = 0; *expression_count = 0;
    if (!app || !app->config.model.behavior) return;
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatNeoBehaviorEntry *entry = &app->behaviors.entries[i];
        if (entry->kind == BONGO_CAT_NEO_BEHAVIOR_MOTION && motions)
            motions[(*motion_count)++] = entry->label;
        else if (entry->kind == BONGO_CAT_NEO_BEHAVIOR_EXPRESSION && expressions)
            expressions[(*expression_count)++] = entry->label;
    }
}

bool bongo_cat_neo_window_behavior_action(BongoCatNeoApp *app,
    BongoCatNeoMenuAction action) {
    BongoCatNeoBehaviorKind kind;
    size_t position;
    if (action >= BONGO_CAT_NEO_MENU_MOTION_FIRST &&
        action < BONGO_CAT_NEO_MENU_MOTION_FIRST + BONGO_CAT_NEO_BEHAVIOR_CAP) {
        kind = BONGO_CAT_NEO_BEHAVIOR_MOTION;
        position = (size_t)(action - BONGO_CAT_NEO_MENU_MOTION_FIRST);
    } else if (action >= BONGO_CAT_NEO_MENU_EXPRESSION_FIRST &&
        action < BONGO_CAT_NEO_MENU_EXPRESSION_FIRST + BONGO_CAT_NEO_BEHAVIOR_CAP) {
        kind = BONGO_CAT_NEO_BEHAVIOR_EXPRESSION;
        position = (size_t)(action - BONGO_CAT_NEO_MENU_EXPRESSION_FIRST);
    } else return false;
    const BongoCatNeoBehaviorEntry *entry = nth_behavior(app, kind, position);
    return entry && run_binding(app, entry);
}

bool bongo_cat_neo_window_behavior_self_test(BongoCatNeoApp *app) {
    if (!app) return false;
    bool enabled = app->config.model.behavior, passed = true;
    app->config.model.behavior = true;
    if (nth_behavior(app, BONGO_CAT_NEO_BEHAVIOR_MOTION, 0))
        passed = bongo_cat_neo_window_behavior_action(app,
            BONGO_CAT_NEO_MENU_MOTION_FIRST) && passed;
    if (nth_behavior(app, BONGO_CAT_NEO_BEHAVIOR_EXPRESSION, 0))
        passed = bongo_cat_neo_window_behavior_action(app,
            BONGO_CAT_NEO_MENU_EXPRESSION_FIRST) && passed;
    app->config.model.behavior = enabled;
    return passed;
}
