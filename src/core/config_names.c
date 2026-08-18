#include "bongo_cat/config.h"

#include <string.h>

const char *bongo_cat_theme_name(BongoCatTheme value) {
    const char *names[] = {"auto", "light", "dark"};
    return (unsigned)value <= BONGO_CAT_THEME_DARK ? names[value] : names[0];
}

const char *bongo_cat_language_name(BongoCatLanguage value) {
    const char *names[] = {"en-US", "zh-CN", "zh-Hant", "fr-FR", "de-DE",
        "ja-JP", "ko-KR", "pt-BR", "ru-RU", "es-ES"};
    return (unsigned)value < BONGO_CAT_LANG_COUNT ? names[value] : names[0];
}

bool bongo_cat_language_parse(const char *name, BongoCatLanguage *value) {
    if (!name || !value) return false;
    if (!strcmp(name, "zh-TW")) {
        *value = BONGO_CAT_LANG_ZH_HANT;
        return true;
    }
    for (int i = 0; i < BONGO_CAT_LANG_COUNT; ++i) {
        if (strcmp(name, bongo_cat_language_name((BongoCatLanguage)i)))
            continue;
        *value = (BongoCatLanguage)i;
        return true;
    }
    return false;
}

const char *bongo_cat_mode_name(BongoCatModelMode value) {
    const char *names[] = {"standard", "keyboard", "gamepad"};
    return (unsigned)value <= BONGO_CAT_MODE_GAMEPAD ? names[value] : names[0];
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
