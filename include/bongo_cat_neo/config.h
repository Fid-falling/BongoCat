#ifndef BONGO_CAT_NEO_CONFIG_H
#define BONGO_CAT_NEO_CONFIG_H

#include "bongo_cat_neo/common.h"

typedef enum BongoCatNeoTheme { BONGO_CAT_NEO_THEME_AUTO, BONGO_CAT_NEO_THEME_LIGHT, BONGO_CAT_NEO_THEME_DARK } BongoCatNeoTheme;
typedef enum BongoCatNeoLanguage {
    BONGO_CAT_NEO_LANG_EN_US,
    BONGO_CAT_NEO_LANG_ZH_CN,
    BONGO_CAT_NEO_LANG_ZH_TW,
    BONGO_CAT_NEO_LANG_PT_BR,
    BONGO_CAT_NEO_LANG_VI_VN
} BongoCatNeoLanguage;
typedef enum BongoCatNeoModelMode {
    BONGO_CAT_NEO_MODE_STANDARD,
    BONGO_CAT_NEO_MODE_KEYBOARD,
    BONGO_CAT_NEO_MODE_GAMEPAD
} BongoCatNeoModelMode;

typedef struct BongoCatNeoModelOptions {
    bool mirror;
    bool mouse_mirror;
    bool motion_sound;
    bool behavior;
    bool ignore_mouse;
    float auto_release_seconds;
    int max_fps;
} BongoCatNeoModelOptions;

typedef struct BongoCatNeoWindowOptions {
    bool visible;
    bool pass_through;
    bool always_on_top;
    bool taskbar_visible;
    bool hide_on_hover;
    bool keep_in_screen;
    float scale_percent;
    float opacity_percent;
    float hide_delay_seconds;
    int x;
    int y;
    int width;
    int height;
} BongoCatNeoWindowOptions;

typedef struct BongoCatNeoAppOptions {
    bool autostart;
    bool tray_visible;
    BongoCatNeoTheme theme;
    BongoCatNeoLanguage language;
} BongoCatNeoAppOptions;

typedef struct BongoCatNeoShortcutOptions {
    char visible_cat[BONGO_CAT_NEO_SHORTCUT_CAP];
    char visible_preferences[BONGO_CAT_NEO_SHORTCUT_CAP];
    char mirror[BONGO_CAT_NEO_SHORTCUT_CAP];
    char pass_through[BONGO_CAT_NEO_SHORTCUT_CAP];
    char always_on_top[BONGO_CAT_NEO_SHORTCUT_CAP];
} BongoCatNeoShortcutOptions;

typedef struct BongoCatNeoBehaviorShortcut {
    char id[BONGO_CAT_NEO_PATH_CAP];
    char shortcut[BONGO_CAT_NEO_SHORTCUT_CAP];
} BongoCatNeoBehaviorShortcut;

typedef struct BongoCatNeoConfig {
    BongoCatNeoModelOptions model;
    BongoCatNeoWindowOptions window;
    BongoCatNeoAppOptions app;
    BongoCatNeoShortcutOptions shortcuts;
    BongoCatNeoBehaviorShortcut behavior_shortcuts[BONGO_CAT_NEO_BEHAVIOR_CAP];
    size_t behavior_shortcut_count;
    char current_model[BONGO_CAT_NEO_PATH_CAP];
    BongoCatNeoModelMode current_mode;
} BongoCatNeoConfig;

#ifdef __cplusplus
extern "C" {
#endif

void bongo_cat_neo_config_defaults(BongoCatNeoConfig *config);
void bongo_cat_neo_config_validate(BongoCatNeoConfig *config);
BongoCatNeoResult bongo_cat_neo_preferences_load(const char *path,
    BongoCatNeoConfig *config, BongoCatNeoError *error);
BongoCatNeoResult bongo_cat_neo_preferences_save(const char *path,
    const BongoCatNeoConfig *config, BongoCatNeoError *error);
BongoCatNeoResult bongo_cat_neo_session_load(const char *path,
    BongoCatNeoConfig *config, BongoCatNeoError *error);
BongoCatNeoResult bongo_cat_neo_session_save(const char *path,
    const BongoCatNeoConfig *config, BongoCatNeoError *error);
const char *bongo_cat_neo_theme_name(BongoCatNeoTheme value);
const char *bongo_cat_neo_language_name(BongoCatNeoLanguage value);
const char *bongo_cat_neo_mode_name(BongoCatNeoModelMode value);

#ifdef __cplusplus
}
#endif

#endif
