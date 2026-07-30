#include "bongo_cat/config.h"

#include <string.h>

static float clampf(float value, float low, float high) {
    return value < low ? low : value > high ? high : value;
}

void bongo_cat_config_defaults(BongoCatConfig *config) {
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->model.motion_sound = true;
    config->model.behavior = true;
    config->model.auto_release_seconds = 3.0f;
    config->model.max_fps = 60;
    config->window.visible = true;
    config->window.always_on_top = true;
    config->window.keep_in_screen = true;
    config->window.scale_percent = 100.0f;
    config->window.opacity_percent = 100.0f;
    config->window.width = 612;
    config->window.height = 354;
    config->app.tray_visible = true;
    config->app.theme = BONGO_CAT_THEME_AUTO;
    config->app.language = BONGO_CAT_LANG_EN_US;
    config->current_mode = BONGO_CAT_MODE_GAMEPAD;
    memcpy(config->current_model, "standard", sizeof("standard"));
}

void bongo_cat_config_validate(BongoCatConfig *config) {
    if (!config) return;
    config->model.auto_release_seconds = clampf(config->model.auto_release_seconds, 0.05f, 30.0f);
    if (config->model.max_fps < 1) config->model.max_fps = 1;
    if (config->model.max_fps > 240) config->model.max_fps = 240;
    config->window.scale_percent = clampf(config->window.scale_percent, 10.0f, 500.0f);
    config->window.opacity_percent = clampf(config->window.opacity_percent, 10.0f, 100.0f);
    config->window.hide_delay_seconds = clampf(config->window.hide_delay_seconds, 0.0f, 60.0f);
    if (config->window.width < 64) config->window.width = 64;
    if (config->window.height < 64) config->window.height = 64;
    if (config->window.width > 8192) config->window.width = 8192;
    if (config->window.height > 8192) config->window.height = 8192;
    if (config->app.theme > BONGO_CAT_THEME_DARK) config->app.theme = BONGO_CAT_THEME_AUTO;
    if (config->app.language > BONGO_CAT_LANG_VI_VN) config->app.language = BONGO_CAT_LANG_EN_US;
    if (config->current_mode > BONGO_CAT_MODE_GAMEPAD) config->current_mode = BONGO_CAT_MODE_STANDARD;
    config->current_model[sizeof(config->current_model) - 1] = '\0';
    if (config->behavior_shortcut_count > BONGO_CAT_BEHAVIOR_CAP)
        config->behavior_shortcut_count = BONGO_CAT_BEHAVIOR_CAP;
    for (size_t i = 0; i < config->behavior_shortcut_count; ++i) {
        config->behavior_shortcuts[i].id[BONGO_CAT_PATH_CAP - 1] = '\0';
        config->behavior_shortcuts[i].shortcut[BONGO_CAT_SHORTCUT_CAP - 1] = '\0';
    }
}

const char *bongo_cat_theme_name(BongoCatTheme value) {
    const char *names[] = {"auto", "light", "dark"};
    return value <= BONGO_CAT_THEME_DARK ? names[value] : names[0];
}

const char *bongo_cat_language_name(BongoCatLanguage value) {
    const char *names[] = {"en-US", "zh-CN", "zh-TW", "pt-BR", "vi-VN"};
    return value <= BONGO_CAT_LANG_VI_VN ? names[value] : names[0];
}

const char *bongo_cat_mode_name(BongoCatModelMode value) {
    const char *names[] = {"standard", "keyboard", "gamepad"};
    return value <= BONGO_CAT_MODE_GAMEPAD ? names[value] : names[0];
}
