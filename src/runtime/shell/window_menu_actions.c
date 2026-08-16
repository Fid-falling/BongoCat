#include "runtime.h"
#include "window_menu.h"
#include "bongo_cat/i18n.h"
#include "bongo_cat/preferences.h"
#include "preferences_notice.h"

#include <stdio.h>
#include <string.h>

static const char *tr(BongoCatApp *app, const char *key,
    const char *fallback) {
    return bongo_cat_i18n_get(app->i18n, key, fallback);
}

static bool select_model(BongoCatApp *app, const char *id) {
    BongoCatError error = {0};
    if (bongo_cat_app_select_model_with_error(app, id, &error)) return true;
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Model switch failed: %s",
        error.message[0] ? error.message : "unknown error");
    const char *message = tr(app, "native.modelLoadFailed",
        "Unable to display this model");
    if (app->preferences && bongo_cat_preferences_visible(app->preferences))
        bongo_cat_preferences_notice_show(app, message, true);
    else SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, BONGO_CAT_NAME,
        message, app->window);
    return false;
}

void bongo_cat_window_show_context_menu(BongoCatApp *app) {
    BongoCatWindowMenuPreview preview;
    bongo_cat_window_menu_preview_init(&preview, app);
    const char *model_names[BONGO_CAT_MODEL_CAP];
    size_t current_model = app->models.count;
    for (size_t i = 0; i < app->models.count; ++i) {
        model_names[i] = bongo_cat_model_name(&app->settings,
            &app->models.entries[i]);
        if (!strcmp(app->models.entries[i].id, app->session.active_model_id))
            current_model = i;
    }
    char motion_names[BONGO_CAT_BEHAVIOR_CAP][BONGO_CAT_MENU_LABEL_CAP];
    char expression_names[BONGO_CAT_BEHAVIOR_CAP][BONGO_CAT_MENU_LABEL_CAP];
    bool motion_checked[BONGO_CAT_BEHAVIOR_CAP] = {false};
    size_t motion_count, expression_count, current_expression;
    bongo_cat_window_behavior_labels(app, motion_names, motion_checked,
        &motion_count, expression_names, &expression_count,
        &current_expression);
    bool dark_theme = app->settings.app.theme == BONGO_CAT_THEME_DARK ||
        (app->settings.app.theme == BONGO_CAT_THEME_AUTO &&
            SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK);
    BongoCatMenuLabels labels = {
        tr(app, "composables.useAppMenu.labels.preference", "Preferences"),
        tr(app, "composables.useAppMenu.labels.hideCat", "Hide Cat"),
        tr(app, "composables.useAppMenu.labels.passThrough", "Pass Through"),
        tr(app, "composables.useAppMenu.labels.alwaysOnTop", "Always on top"),
        tr(app, "composables.useAppMenu.labels.windowSize", "Window Size"),
        tr(app, "composables.useAppMenu.labels.opacity", "Opacity"),
        tr(app, "composables.useAppMenu.labels.model", "Model"),
        tr(app, "composables.useAppMenu.labels.addModel", "Add Model"),
        tr(app, "composables.useAppMenu.labels.quitApp", "Exit"),
        tr(app, "composables.useAppMenu.labels.wheelSizeHint", "Wheel: resize"),
        tr(app, "composables.useAppMenu.labels.wheelOpacityHint",
            "Ctrl+Wheel: opacity"),
        tr(app, "composables.useAppMenu.labels.motion", "Motions"),
        tr(app, "composables.useAppMenu.labels.expression", "Expressions"),
        model_names, motion_names, expression_names, motion_checked,
        app->models.count, current_model, motion_count, expression_count,
        current_expression, app->session.window.scale_percent,
        app->session.window.opacity_percent, app->settings.window.pass_through,
        app->settings.window.always_on_top, dark_theme,
        bongo_cat_window_menu_preview, bongo_cat_window_menu_preview_tick,
        bongo_cat_window_menu_restore, &preview};
    BongoCatMenuAction action = bongo_cat_platform_context_menu(
        &app->platform, &labels);
    if (bongo_cat_window_menu_preview_applied(&preview, action))
        bongo_cat_preferences_invalidate(app->preferences);
    else bongo_cat_window_menu_action(app, action);
}

void bongo_cat_window_menu_action(BongoCatApp *app,
    BongoCatMenuAction action) {
    if (action == BONGO_CAT_MENU_PREFERENCES)
        bongo_cat_preferences_show(app->preferences);
    else if (action == BONGO_CAT_MENU_MODEL_ADD)
        bongo_cat_preferences_open_model_import(app->preferences, app->window);
    else if (action == BONGO_CAT_MENU_HIDE)
        bongo_cat_window_set_visible(app, false);
    else if (action == BONGO_CAT_MENU_PASS_THROUGH) {
        app->settings.window.pass_through = !app->settings.window.pass_through;
        bongo_cat_window_mark_hit_dirty(app);
        bongo_cat_window_sync_click_through(app);
    } else if (action == BONGO_CAT_MENU_ALWAYS_ON_TOP) {
        app->settings.window.always_on_top = !app->settings.window.always_on_top;
        bongo_cat_platform_set_always_on_top(&app->platform,
            app->settings.window.always_on_top);
        bongo_cat_window_mark_hit_dirty(app);
        bongo_cat_window_sync_click_through(app);
    } else if (action >= BONGO_CAT_MENU_SCALE_50 &&
        action <= BONGO_CAT_MENU_SCALE_200) {
        bongo_cat_window_cancel_wheel_animation(app);
        bongo_cat_window_set_scale(app,
            (float)(50 + 10 * (action - BONGO_CAT_MENU_SCALE_50)));
    } else if (action >= BONGO_CAT_MENU_OPACITY_10 &&
        action <= BONGO_CAT_MENU_OPACITY_100) {
        bongo_cat_window_cancel_wheel_animation(app);
        app->session.window.opacity_percent =
            (float)(10 * (action - BONGO_CAT_MENU_OPACITY_10 + 1));
        bongo_cat_platform_set_opacity(&app->platform,
            app->session.window.opacity_percent / 100.0f);
    } else if (bongo_cat_window_behavior_action(app, action)) {
        bongo_cat_app_render_now(app);
    } else if (action >= BONGO_CAT_MENU_MODEL_FIRST &&
        action < BONGO_CAT_MENU_MODEL_FIRST + BONGO_CAT_MODEL_CAP) {
        size_t index = (size_t)(action - BONGO_CAT_MENU_MODEL_FIRST);
        if (index < app->models.count)
            select_model(app, app->models.entries[index].id);
    } else if (action == BONGO_CAT_MENU_EXIT) app->running = false;
    bongo_cat_preferences_invalidate(app->preferences);
}

bool bongo_cat_window_menu_self_test(BongoCatApp *app) {
    if (!app || !app->preferences) return false;
    app->settings.window.pass_through = false;
    app->settings.window.always_on_top = false;
    app->session.window.scale_percent = 100.0f;
    app->session.window.opacity_percent = 100.0f;
    bongo_cat_window_menu_action(app, BONGO_CAT_MENU_PASS_THROUGH);
    bongo_cat_window_menu_action(app, BONGO_CAT_MENU_ALWAYS_ON_TOP);
    bongo_cat_window_menu_action(app, BONGO_CAT_MENU_SCALE_120);
    bongo_cat_window_menu_action(app, BONGO_CAT_MENU_OPACITY_50);
    bongo_cat_window_menu_action(app, BONGO_CAT_MENU_PREFERENCES);
    bool behavior = bongo_cat_window_behavior_self_test(app);
    bool result = app->settings.window.pass_through &&
        app->settings.window.always_on_top &&
        app->session.window.scale_percent == 120.0f &&
        app->session.window.opacity_percent == 50.0f &&
        bongo_cat_preferences_visible(app->preferences) && behavior;
    bongo_cat_preferences_close(app->preferences);
    return result;
}
