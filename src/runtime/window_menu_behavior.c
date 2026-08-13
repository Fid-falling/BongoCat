#include "window_menu.h"
#include "bongo_cat/audio.h"

#include <stdio.h>
#include <string.h>

static const BongoCatBehaviorEntry *nth_behavior(BongoCatApp *app,
    BongoCatBehaviorKind kind, size_t position) {
    if (!app) return NULL;
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatBehaviorEntry *entry = &app->behaviors.entries[i];
        if (entry->kind != kind) continue;
        if (kind == BONGO_CAT_BEHAVIOR_MOTION &&
            !bongo_cat_live2d_motion_visible(app->live2d,
                entry->group, entry->index)) continue;
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

static const char *behavior_label(const BongoCatApp *app,
    const BongoCatBehaviorEntry *entry) {
    if (!app || !entry) return "";
    for (size_t i = 0; i < app->config.behavior_shortcut_count; ++i) {
        const BongoCatBehaviorShortcut *binding = &app->config.behavior_shortcuts[i];
        if (strcmp(binding->id, entry->id) == 0 && binding->label[0])
            return binding->label;
    }
    return entry->label;
}

static void menu_label(char output[BONGO_CAT_MENU_LABEL_CAP],
    const BongoCatApp *app, const BongoCatBehaviorEntry *entry) {
    const char *label = behavior_label(app, entry);
    const char *shortcut = behavior_shortcut(app, entry->id);
    if (shortcut && shortcut[0]) snprintf(output, BONGO_CAT_MENU_LABEL_CAP,
        "%s - %s", label, shortcut);
    else snprintf(output, BONGO_CAT_MENU_LABEL_CAP, "%s", label);
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
    char motions[][BONGO_CAT_MENU_LABEL_CAP], bool *motion_checked,
    size_t *motion_count, char expressions[][BONGO_CAT_MENU_LABEL_CAP],
    size_t *expression_count,
    size_t *current_expression) {
    if (!motion_count || !expression_count) return;
    *motion_count = 0; *expression_count = 0;
    if (current_expression) *current_expression = BONGO_CAT_BEHAVIOR_CAP;
    if (!app) return;
    int active_expression = bongo_cat_live2d_expression(app->live2d);
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatBehaviorEntry *entry = &app->behaviors.entries[i];
        if (entry->kind == BONGO_CAT_BEHAVIOR_MOTION && motions) {
            if (!bongo_cat_live2d_motion_visible(app->live2d,
                entry->group, entry->index)) continue;
            if (motion_checked) motion_checked[*motion_count] =
                bongo_cat_live2d_motion_selected(app->live2d,
                    entry->group, entry->index);
            menu_label(motions[*motion_count], app, entry);
            (*motion_count)++;
        } else if (entry->kind == BONGO_CAT_BEHAVIOR_EXPRESSION && expressions) {
            if (current_expression && entry->index == active_expression)
                *current_expression = *expression_count;
            menu_label(expressions[*expression_count], app, entry);
            (*expression_count)++;
        }
    }
    if (current_expression && active_expression < 0 && *expression_count)
        *current_expression = 0;
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

bool bongo_cat_window_behavior_preview(BongoCatApp *app,
    BongoCatMenuAction action) {
    if (!app) return false;
    if (action >= BONGO_CAT_MENU_MOTION_FIRST &&
        action < BONGO_CAT_MENU_MOTION_FIRST + BONGO_CAT_BEHAVIOR_CAP) {
        size_t position = (size_t)(action - BONGO_CAT_MENU_MOTION_FIRST);
        const BongoCatBehaviorEntry *entry = nth_behavior(app,
            BONGO_CAT_BEHAVIOR_MOTION, position);
        return entry && bongo_cat_live2d_preview_motion(app->live2d,
            entry->group, entry->index);
    }
    if (action >= BONGO_CAT_MENU_EXPRESSION_FIRST &&
        action < BONGO_CAT_MENU_EXPRESSION_FIRST + BONGO_CAT_BEHAVIOR_CAP) {
        size_t position = (size_t)(action - BONGO_CAT_MENU_EXPRESSION_FIRST);
        const BongoCatBehaviorEntry *entry = nth_behavior(app,
            BONGO_CAT_BEHAVIOR_EXPRESSION, position);
        return entry && bongo_cat_live2d_set_expression(app->live2d,
            entry->index);
    }
    return false;
}

bool bongo_cat_window_behavior_commit_preview(BongoCatApp *app,
    BongoCatMenuAction action) {
    if (!app || action < BONGO_CAT_MENU_MOTION_FIRST ||
        action >= BONGO_CAT_MENU_MOTION_FIRST + BONGO_CAT_BEHAVIOR_CAP)
        return false;
    const BongoCatBehaviorEntry *entry = nth_behavior(app,
        BONGO_CAT_BEHAVIOR_MOTION,
        (size_t)(action - BONGO_CAT_MENU_MOTION_FIRST));
    if (!entry || !bongo_cat_live2d_commit_motion_preview(app->live2d,
        entry->group, entry->index)) return false;
    if (entry->sound[0]) {
        BongoCatError error = {0};
        bongo_cat_audio_play(app->audio, entry->sound, &error);
    }
    const char *shortcut = behavior_shortcut(app, entry->id);
    if (shortcut && shortcut[0]) for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatBehaviorEntry *companion = &app->behaviors.entries[i];
        const char *bound = behavior_shortcut(app, companion->id);
        if (companion != entry && bound && strcmp(bound, shortcut) == 0)
            bongo_cat_app_run_behavior(app, companion);
    }
    app->dirty = true;
    return true;
}

bool bongo_cat_window_behavior_menu_action(BongoCatMenuAction action) {
    return (action >= BONGO_CAT_MENU_MOTION_FIRST &&
        action < BONGO_CAT_MENU_MOTION_FIRST + BONGO_CAT_BEHAVIOR_CAP) ||
        (action >= BONGO_CAT_MENU_EXPRESSION_FIRST &&
            action < BONGO_CAT_MENU_EXPRESSION_FIRST + BONGO_CAT_BEHAVIOR_CAP);
}

static bool expression_self_test(BongoCatApp *app) {
    size_t count = 0;
    while (nth_behavior(app, BONGO_CAT_BEHAVIOR_EXPRESSION, count)) count++;
    if (!count) return true;
    int original = bongo_cat_live2d_expression(app->live2d);
    char motions[BONGO_CAT_BEHAVIOR_CAP][BONGO_CAT_MENU_LABEL_CAP];
    bool motion_checked[BONGO_CAT_BEHAVIOR_CAP] = {false};
    char expressions[BONGO_CAT_BEHAVIOR_CAP][BONGO_CAT_MENU_LABEL_CAP];
    size_t motion_count, expression_count, current_expression;
    bool passed = bongo_cat_live2d_set_expression(app->live2d, -1);
    bongo_cat_window_behavior_labels(app, motions, motion_checked, &motion_count,
        expressions, &expression_count, &current_expression);
    passed = passed && expression_count == count && current_expression == 0;
    size_t selected_position = count > 2 ? 2 : count - 1;
    const BongoCatBehaviorEntry *selected = nth_behavior(app,
        BONGO_CAT_BEHAVIOR_EXPRESSION, selected_position);
    passed = selected && bongo_cat_window_behavior_action(app,
        BONGO_CAT_MENU_EXPRESSION_FIRST + selected_position) &&
        bongo_cat_live2d_expression(app->live2d) == selected->index && passed;
    bongo_cat_window_behavior_labels(app, motions, motion_checked, &motion_count,
        expressions, &expression_count, &current_expression);
    passed = passed && expression_count == count &&
        current_expression == selected_position;

    if (passed && count > 1) {
        size_t alternate_position = selected_position ? 0 : 1;
        const BongoCatBehaviorEntry *alternate = nth_behavior(app,
            BONGO_CAT_BEHAVIOR_EXPRESSION, alternate_position);
        BongoCatWindowMenuPreview preview;
        bongo_cat_window_menu_preview_init(&preview, app);
        bongo_cat_window_menu_preview(&preview,
            BONGO_CAT_MENU_EXPRESSION_FIRST + alternate_position);
        passed = alternate && bongo_cat_live2d_expression(app->live2d) ==
            alternate->index;
        bongo_cat_window_menu_restore(&preview, BONGO_CAT_MENU_NONE);
        passed = passed && bongo_cat_live2d_expression(app->live2d) ==
            selected->index;

        bongo_cat_window_menu_preview_init(&preview, app);
        bongo_cat_window_menu_preview(&preview,
            BONGO_CAT_MENU_EXPRESSION_FIRST + alternate_position);
        bongo_cat_window_menu_restore(&preview,
            BONGO_CAT_MENU_EXPRESSION_FIRST + alternate_position);
        passed = passed && bongo_cat_live2d_expression(app->live2d) ==
            alternate->index && bongo_cat_window_menu_preview_applied(&preview,
                BONGO_CAT_MENU_EXPRESSION_FIRST + alternate_position);
    }
    bool restored = bongo_cat_live2d_set_expression(app->live2d, original);
    if (!passed || !restored) SDL_Log("Expression menu self-test failed: "
        "count=%llu mapped=%llu current=%llu original=%d restored=%d",
        (unsigned long long)count, (unsigned long long)expression_count,
        (unsigned long long)current_expression, original, restored);
    return restored && passed;
}

bool bongo_cat_window_behavior_self_test(BongoCatApp *app) {
    if (!app) return false;
    bool passed = true;
    if (nth_behavior(app, BONGO_CAT_BEHAVIOR_MOTION, 0)) {
        const BongoCatBehaviorEntry *first = nth_behavior(app,
            BONGO_CAT_BEHAVIOR_MOTION, 0);
        passed = bongo_cat_window_behavior_action(app,
            BONGO_CAT_MENU_MOTION_FIRST) && passed;
        char motions[BONGO_CAT_BEHAVIOR_CAP][BONGO_CAT_MENU_LABEL_CAP];
        bool checked[BONGO_CAT_BEHAVIOR_CAP] = {false};
        bool before[BONGO_CAT_BEHAVIOR_CAP] = {false};
        char expressions[BONGO_CAT_BEHAVIOR_CAP][BONGO_CAT_MENU_LABEL_CAP];
        size_t motion_count, expression_count, current_expression;
        bongo_cat_window_behavior_labels(app, motions, checked, &motion_count,
            expressions, &expression_count, &current_expression);
        passed = motion_count > 0 && checked[0] && passed;
        bool initial_passed = passed;
        const char *first_shortcut = behavior_shortcut(app, first->id);
        if (first_shortcut && first_shortcut[0])
            passed = strstr(motions[0], first_shortcut) != NULL && passed;
        memcpy(before, checked, motion_count * sizeof(before[0]));
        size_t preview_position = nth_behavior(app,
            BONGO_CAT_BEHAVIOR_MOTION, 1) ? 1 : 0;
        BongoCatWindowMenuPreview preview;
        bongo_cat_window_menu_preview_init(&preview, app);
        bongo_cat_window_menu_preview(&preview,
            BONGO_CAT_MENU_MOTION_FIRST + preview_position);
        bongo_cat_window_behavior_labels(app, motions, checked, &motion_count,
            expressions, &expression_count, &current_expression);
        bool preview_stable = memcmp(before, checked,
            motion_count * sizeof(before[0])) == 0;
        passed = preview_stable && passed;
        bongo_cat_window_menu_restore(&preview, BONGO_CAT_MENU_NONE);
        passed = bongo_cat_live2d_motion_selected(app->live2d,
            first->group, first->index) && passed;
        passed = bongo_cat_window_behavior_action(app,
            BONGO_CAT_MENU_MOTION_FIRST + preview_position) && passed;
        bongo_cat_window_behavior_labels(app, motions, checked, &motion_count,
            expressions, &expression_count, &current_expression);
        passed = checked[preview_position] && passed;
        bool independent = false;
        for (size_t i = 1; i < motion_count && !independent; ++i) {
            passed = bongo_cat_window_behavior_action(app,
                BONGO_CAT_MENU_MOTION_FIRST) && passed;
            passed = bongo_cat_window_behavior_action(app,
                BONGO_CAT_MENU_MOTION_FIRST + i) && passed;
            bool first_selected = bongo_cat_live2d_motion_selected(app->live2d,
                first->group, first->index);
            bool candidate_selected = bongo_cat_live2d_motion_selected(app->live2d,
                nth_behavior(app, BONGO_CAT_BEHAVIOR_MOTION, i)->group,
                nth_behavior(app, BONGO_CAT_BEHAVIOR_MOTION, i)->index);
            independent = independent || (first_selected && candidate_selected);
        }
        if (motion_count > 2 && strcmp(nth_behavior(app,
            BONGO_CAT_BEHAVIOR_MOTION, 0)->group, "CAT_motion_lock") == 0)
            passed = independent && passed;
        passed = bongo_cat_window_behavior_action(app,
            BONGO_CAT_MENU_MOTION_FIRST) && passed;
        bool paired = false;
        for (size_t i = 0; i < app->behaviors.count; ++i) {
            const BongoCatBehaviorEntry *item = &app->behaviors.entries[i];
            if (item->kind == BONGO_CAT_BEHAVIOR_MOTION &&
                strcmp(item->group, first->group) == 0 &&
                (item->index == first->index - 1 ||
                    item->index == first->index + 1) &&
                !bongo_cat_live2d_motion_visible(app->live2d,
                    item->group, item->index)) paired = true;
        }
        bool toggled_off = !bongo_cat_live2d_motion_selected(app->live2d,
            first->group, first->index);
        passed = (!paired || toggled_off) && passed;
        if (!passed) SDL_Log("Motion menu self-test failed: initial=%d "
            "preview=%d count=%llu independent=%d off=%d",
            initial_passed, preview_stable, (unsigned long long)motion_count,
            independent, toggled_off);
    }
    passed = expression_self_test(app) && passed;
    return passed;
}
