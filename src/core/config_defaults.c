#include "bongo_cat/config.h"
#include "bongo_cat/utf8.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static float clampf(float value, float low, float high) {
    return value < low ? low : value > high ? high : value;
}

static bool shortcut_equal(const char *left, const char *right) {
    if (!left || !right || !left[0] || !right[0]) return false;
    while (*left && *right) {
        if (tolower((unsigned char)*left++) != tolower((unsigned char)*right++))
            return false;
    }
    return *left == *right;
}

bool bongo_cat_settings_shortcut_conflicts(const BongoCatSettings *config,
    const char *shortcut, const char *exclude) {
    if (!config || !shortcut || !shortcut[0]) return false;
    const char *global[] = {config->shortcuts.visible_cat,
        config->shortcuts.visible_preferences, config->shortcuts.mirror,
        config->shortcuts.pass_through, config->shortcuts.always_on_top};
    for (size_t i = 0; i < sizeof(global) / sizeof(global[0]); ++i)
        if (global[i] != exclude && shortcut_equal(global[i], shortcut))
            return true;
    for (size_t i = 0; i < config->behavior_shortcut_count; ++i) {
        const char *bound = config->behavior_shortcuts[i].shortcut;
        if (bound != exclude && shortcut_equal(bound, shortcut)) return true;
    }
    return false;
}

const char *bongo_cat_settings_model_label(const BongoCatSettings *config,
    const char *id) {
    if (!config || !id) return NULL;
    for (size_t i = 0; i < config->model_label_count; ++i)
        if (!strcmp(config->model_labels[i].id, id))
            return config->model_labels[i].label;
    return NULL;
}

bool bongo_cat_settings_set_model_label(BongoCatSettings *config,
    const char *id, const char *label) {
    if (!config || !id || !id[0]) return false;
    size_t index = config->model_label_count;
    for (size_t i = 0; i < config->model_label_count; ++i)
        if (!strcmp(config->model_labels[i].id, id)) {
            index = i; break;
        }
    if (!label || !label[0]) {
        if (index == config->model_label_count) return false;
        if (index + 1 < config->model_label_count)
            memmove(&config->model_labels[index],
                &config->model_labels[index + 1],
                (config->model_label_count - index - 1) *
                sizeof(config->model_labels[0]));
        config->model_label_count--;
        memset(&config->model_labels[config->model_label_count], 0,
            sizeof(config->model_labels[0]));
        return true;
    }
    if (index < config->model_label_count) {
        if (!strcmp(config->model_labels[index].label, label)) return false;
        snprintf(config->model_labels[index].label,
            sizeof(config->model_labels[index].label), "%s", label);
        return true;
    }
    if (config->model_label_count >= BONGO_CAT_MODEL_CAP) return false;
    BongoCatModelLabel *value =
        &config->model_labels[config->model_label_count++];
    memset(value, 0, sizeof(*value));
    snprintf(value->id, sizeof(value->id), "%s", id);
    snprintf(value->label, sizeof(value->label), "%s", label);
    return true;
}

static void validate_shortcuts(BongoCatSettings *config) {
    char *global[] = {config->shortcuts.visible_cat,
        config->shortcuts.visible_preferences, config->shortcuts.mirror,
        config->shortcuts.pass_through, config->shortcuts.always_on_top};
    for (size_t i = 0; i < sizeof(global) / sizeof(global[0]); ++i) {
        global[i][BONGO_CAT_SHORTCUT_CAP - 1] = '\0';
        for (size_t j = 0; j < i; ++j)
            if (shortcut_equal(global[i], global[j])) global[i][0] = '\0';
    }
    for (size_t i = 0; i < config->behavior_shortcut_count; ++i) {
        char *shortcut = config->behavior_shortcuts[i].shortcut;
        bool duplicate = false;
        for (size_t j = 0; j < sizeof(global) / sizeof(global[0]); ++j)
            duplicate = duplicate || shortcut_equal(shortcut, global[j]);
        for (size_t j = 0; j < i; ++j)
            duplicate = duplicate || shortcut_equal(shortcut,
                config->behavior_shortcuts[j].shortcut);
        if (duplicate) shortcut[0] = '\0';
    }
}

void bongo_cat_settings_defaults(BongoCatSettings *config) {
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->model.auto_release_seconds =
        BONGO_CAT_DEFAULT_AUTO_RELEASE_SECONDS;
    config->model.mouse_centered = true;
    config->model.max_fps = BONGO_CAT_DEFAULT_MAX_FPS;
    config->window.always_on_top = true;
    config->window.keep_in_screen = false;
    config->window.obs_background_color = BONGO_CAT_OBS_BACKGROUND_GREEN;
    config->app.tray_visible = true;
    config->app.theme = BONGO_CAT_THEME_AUTO;
    config->app.language = BONGO_CAT_LANG_EN_US;
    memcpy(config->extensions_json, "{}", sizeof("{}"));
}

void bongo_cat_session_defaults(BongoCatSessionState *session) {
    if (!session) return;
    memset(session, 0, sizeof(*session));
    session->window.visible = true;
    session->window.scale_percent = BONGO_CAT_DEFAULT_WINDOW_SCALE_PERCENT;
    session->window.opacity_percent =
        BONGO_CAT_DEFAULT_WINDOW_OPACITY_PERCENT;
    session->window.width = 612;
    session->window.height = 354;
    memcpy(session->active_model_id, "standard", sizeof("standard"));
}

void bongo_cat_settings_validate(BongoCatSettings *config) {
    if (!config) return;
    config->model.auto_release_seconds = clampf(config->model.auto_release_seconds, 0.05f, 30.0f);
    if (config->model.max_fps < 1) config->model.max_fps = 1;
    if (config->model.max_fps > 240) config->model.max_fps = 240;
    config->window.hide_delay_seconds = clampf(config->window.hide_delay_seconds, 0.0f, 60.0f);
    if ((unsigned)config->window.obs_background_color >=
        BONGO_CAT_OBS_BACKGROUND_COLOR_COUNT)
        config->window.obs_background_color = BONGO_CAT_OBS_BACKGROUND_GREEN;
    if (config->app.theme > BONGO_CAT_THEME_DARK) config->app.theme = BONGO_CAT_THEME_AUTO;
    if ((unsigned)config->app.language >= BONGO_CAT_LANG_COUNT)
        config->app.language = BONGO_CAT_LANG_EN_US;
    if (config->behavior_shortcut_count > BONGO_CAT_BEHAVIOR_BINDING_CAP)
        config->behavior_shortcut_count = BONGO_CAT_BEHAVIOR_BINDING_CAP;
    for (size_t i = 0; i < config->behavior_shortcut_count; ++i) {
        config->behavior_shortcuts[i].id[
            sizeof(config->behavior_shortcuts[i].id) - 1] = '\0';
        config->behavior_shortcuts[i].shortcut[BONGO_CAT_SHORTCUT_CAP - 1] = '\0';
        config->behavior_shortcuts[i].label[BONGO_CAT_ID_CAP - 1] = '\0';
        if (!bongo_cat_utf8_valid(config->behavior_shortcuts[i].label))
            config->behavior_shortcuts[i].label[0] = '\0';
    }
    if (config->model_label_count > BONGO_CAT_MODEL_CAP)
        config->model_label_count = BONGO_CAT_MODEL_CAP;
    for (size_t i = 0; i < config->model_label_count; ++i) {
        config->model_labels[i].id[BONGO_CAT_ID_CAP - 1] = '\0';
        config->model_labels[i].label[BONGO_CAT_ID_CAP - 1] = '\0';
        if (!config->model_labels[i].id[0] ||
            !bongo_cat_utf8_valid(config->model_labels[i].label))
            config->model_labels[i].label[0] = '\0';
    }
    validate_shortcuts(config);
}

void bongo_cat_session_validate(BongoCatSessionState *session) {
    if (!session) return;
    session->window.scale_percent = clampf(session->window.scale_percent,
        10.0f, 500.0f);
    session->window.opacity_percent = clampf(session->window.opacity_percent,
        10.0f, 100.0f);
    if (session->window.width < 64) session->window.width = 64;
    if (session->window.height < 64) session->window.height = 64;
    if (session->window.width > 8192) session->window.width = 8192;
    if (session->window.height > 8192) session->window.height = 8192;
    session->active_model_id[sizeof(session->active_model_id) - 1] = '\0';
    if (!session->active_model_id[0])
        memcpy(session->active_model_id, "standard", sizeof("standard"));
}

const char *bongo_cat_theme_name(BongoCatTheme value) {
    const char *names[] = {"auto", "light", "dark"};
    return value <= BONGO_CAT_THEME_DARK ? names[value] : names[0];
}

const char *bongo_cat_language_name(BongoCatLanguage value) {
    const char *names[] = {"en-US", "zh-CN", "zh-TW", "fr-FR", "de-DE",
        "ja-JP", "ko-KR", "pt-BR", "ru-RU", "es-ES"};
    return (unsigned)value < BONGO_CAT_LANG_COUNT ? names[value] : names[0];
}

const char *bongo_cat_mode_name(BongoCatModelMode value) {
    const char *names[] = {"standard", "keyboard", "gamepad"};
    return value <= BONGO_CAT_MODE_GAMEPAD ? names[value] : names[0];
}

const char *bongo_cat_obs_background_color_name(
    BongoCatObsBackgroundColor value) {
    static const char *names[] = {
        "#00ff00", "#0000ff", "#ff0000", "#ff00ff"};
    return (unsigned)value < BONGO_CAT_OBS_BACKGROUND_COLOR_COUNT ?
        names[value] : names[BONGO_CAT_OBS_BACKGROUND_GREEN];
}

uint32_t bongo_cat_obs_background_color_rgb(
    BongoCatObsBackgroundColor value) {
    static const uint32_t colors[] = {
        0x00ff00, 0x0000ff, 0xff0000, 0xff00ff};
    return (unsigned)value < BONGO_CAT_OBS_BACKGROUND_COLOR_COUNT ?
        colors[value] : colors[BONGO_CAT_OBS_BACKGROUND_GREEN];
}
