#include "preferences_internal.h"
#include "preferences_theme.h"
#include "preferences_widgets.h"
#include "preferences_notice.h"
#include "bongo_cat/audio.h"
#include "bongo_cat/i18n.h"
#include "bongo_cat/preferences.h"
#include "bongo_cat/tray.h"
#include "../runtime/runtime.h"

#include <SDL3/SDL.h>
#include <string.h>

static const char *tr(BongoCatApp *app, const char *key, const char *fallback) {
    return bongo_cat_i18n_get(app->i18n, key, fallback);
}

static void section_gap(struct nk_context *context, float pixels) {
    context->current->layout->at_y += pixels;
}

void bongo_cat_preferences_page_cat(BongoCatApp *app, struct nk_context *context) {
    BongoCatModelOptions *model = &app->config.model;
    BongoCatWindowOptions *window = &app->config.window;
    bongo_cat_pref_section(context, tr(app, "pages.preference.cat.labels.windowSettings",
        "Window Settings"));
    if (bongo_cat_pref_toggle(context, "pass-through", tr(app,
        "composables.useAppMenu.labels.passThrough", "Pass Through"), "",
        &window->pass_through)) {
        bongo_cat_window_mark_hit_dirty(app);
        bongo_cat_window_sync_click_through(app);
    }
    if (bongo_cat_pref_toggle(context, "always-top", tr(app,
        "composables.useAppMenu.labels.alwaysOnTop", "Always on Top"), "",
        &window->always_on_top))
        bongo_cat_platform_set_always_on_top(&app->platform, window->always_on_top);
    if (bongo_cat_pref_toggle(context, "keep-in-screen", tr(app,
        "pages.preference.cat.labels.keepInScreen", "Keep on Screen"), "",
        &window->keep_in_screen) && window->keep_in_screen)
        bongo_cat_window_clamp_to_display(app);
    float old_scale = window->scale_percent;
    bongo_cat_pref_float(context, "window-size", tr(app,
        "pages.preference.cat.labels.windowSize", "Window Size"), tr(app,
        "composables.useAppMenu.labels.wheelSizeHint", "Wheel: resize"),
        10.0f, &window->scale_percent, 500.0f, 1.0f,
        BONGO_CAT_DEFAULT_WINDOW_SCALE_PERCENT);
    if (old_scale != window->scale_percent && old_scale > 0.0f) {
        float requested_scale = window->scale_percent;
        window->scale_percent = old_scale;
        bongo_cat_window_cancel_wheel_animation(app);
        bongo_cat_window_set_scale(app, requested_scale);
    }
    float old_opacity = window->opacity_percent;
    bongo_cat_pref_slider(context, "opacity", tr(app,
        "pages.preference.cat.labels.opacity", "Opacity"), tr(app,
        "composables.useAppMenu.labels.wheelOpacityHint", "Ctrl+Wheel: opacity"),
        10.0f, &window->opacity_percent, 100.0f, 1.0f,
        BONGO_CAT_DEFAULT_WINDOW_OPACITY_PERCENT);
    if (old_opacity != window->opacity_percent) bongo_cat_window_cancel_wheel_animation(app);
    if (old_opacity != window->opacity_percent && !app->hover_hidden)
        bongo_cat_platform_set_opacity(&app->platform,
            window->opacity_percent / 100.0f);

    section_gap(context, 10);
    bongo_cat_pref_section(context, tr(app, "pages.preference.cat.labels.modelSettings",
        "Model Settings"));
    if (bongo_cat_pref_toggle(context, "mirror", tr(app,
        "pages.preference.cat.labels.mirrorMode", "Mirror Mode"), "",
        &model->mirror)) app->dirty = true;
    bongo_cat_pref_toggle(context, "mouse-mirror", tr(app,
        "pages.preference.cat.labels.mouseMirror", "Mouse Mirror"), "",
        &model->mouse_mirror);
    bongo_cat_pref_toggle(context, "ignore-mouse", tr(app,
        "pages.preference.cat.labels.ignoreMouse", "Ignore Mouse Events"), "",
        &model->ignore_mouse);
    if (bongo_cat_pref_toggle(context, "motion-sound", tr(app,
        "pages.preference.cat.labels.motionSound", "Motion Sound"), "",
        &model->motion_sound)) bongo_cat_audio_set_enabled(app->audio, model->motion_sound);
    bongo_cat_pref_toggle(context, "behavior", tr(app,
        "pages.preference.cat.labels.behavior", "Motions and Expressions"), "",
        &model->behavior);
    bongo_cat_pref_float(context, "release-delay", tr(app,
        "pages.preference.cat.labels.autoReleaseDelay", "Auto Release Delay"), "",
        .05f, &model->auto_release_seconds, 30.0f, .05f,
        BONGO_CAT_DEFAULT_AUTO_RELEASE_SECONDS);
    bongo_cat_pref_int(context, "max-fps", tr(app,
        "pages.preference.cat.labels.maxFPS", "Max Frame Rate"), "",
        1, &model->max_fps, 240, 1, BONGO_CAT_DEFAULT_MAX_FPS);
}

static void update_autostart(BongoCatApp *app, bool old_value) {
    BongoCatError error = {0};
    if (bongo_cat_platform_set_autostart(app->config.app.autostart, &error) == BONGO_CAT_OK) return;
    app->config.app.autostart = old_value;
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
    bongo_cat_preferences_notice_show(app, error.message, true);
}

void bongo_cat_preferences_page_general(BongoCatApp *app, struct nk_context *context) {
    BongoCatAppOptions *options = &app->config.app;
    const char *languages[] = {"English", "简体中文", "繁體中文", "Português", "Tiếng Việt"};
    const char *ui_languages[] = {languages[1], languages[2], languages[0],
        languages[3], languages[4]};
    bongo_cat_pref_section(context, tr(app, "pages.preference.general.labels.appSettings",
        "Application Settings"));
    bool old_autostart = options->autostart;
    if (bongo_cat_pref_toggle(context, "autostart", tr(app,
        "pages.preference.general.labels.launchOnStartup", "Launch on Startup"), "",
        &options->autostart)) update_autostart(app, old_autostart);
    if (bongo_cat_pref_toggle(context, "taskbar", tr(app,
        "pages.preference.general.labels.showTaskbarIcon", "Show Taskbar Icon"), tr(app,
        "pages.preference.general.hints.showTaskbarIcon", "Allows window capture in OBS."),
        &app->config.window.taskbar_visible)) {
        bongo_cat_platform_set_taskbar(&app->platform, app->config.window.taskbar_visible);
        app->dirty = true;
    }
    section_gap(context, 7);
    bongo_cat_pref_section(context, tr(app,
        "pages.preference.general.labels.appearanceSettings", "Appearance Settings"));
    section_gap(context, 6);
    const int language_to_ui[] = {2, 0, 1, 3, 4};
    const BongoCatLanguage ui_to_language[] = {
        BONGO_CAT_LANG_ZH_CN, BONGO_CAT_LANG_ZH_TW,
        BONGO_CAT_LANG_EN_US, BONGO_CAT_LANG_PT_BR,
        BONGO_CAT_LANG_VI_VN};
    int selected = bongo_cat_pref_combo(context,
        "language", tr(app, "pages.preference.general.labels.language",
        "Language"), "", ui_languages, 5, language_to_ui[options->language]);
    options->language = ui_to_language[selected];
    const char *themes[] = {
        tr(app, "pages.preference.general.options.auto", "System"),
        tr(app, "pages.preference.general.options.lightMode", "Light"),
        tr(app, "pages.preference.general.options.darkMode", "Dark")};
    options->theme = (BongoCatTheme)bongo_cat_pref_theme(context,
        "theme", tr(app, "pages.preference.general.labels.themeMode",
        "Theme"), themes, options->theme);
}
