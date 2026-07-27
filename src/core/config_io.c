#include "config_internal.h"
#include "bongo_cat_neo/path.h"

#include <stdio.h>
#include <string.h>

#define PREFERENCES_FORMAT "bongo-cat-neo/preferences"

static bool get_bool(yyjson_val *obj, const char *key, bool fallback) {
    yyjson_val *value = yyjson_obj_get(obj, key);
    return yyjson_is_bool(value) ? yyjson_get_bool(value) : fallback;
}
static int get_int(yyjson_val *obj, const char *key, int fallback) {
    yyjson_val *value = yyjson_obj_get(obj, key);
    return yyjson_is_num(value) ? (int)yyjson_get_sint(value) : fallback;
}
static float get_float(yyjson_val *obj, const char *key, float fallback) {
    yyjson_val *value = yyjson_obj_get(obj, key);
    return yyjson_is_num(value) ? (float)yyjson_get_num(value) : fallback;
}
static const char *get_string(yyjson_val *obj, const char *key) {
    yyjson_val *value = yyjson_obj_get(obj, key);
    return yyjson_is_str(value) ? yyjson_get_str(value) : NULL;
}
static void copy_string(char *target, size_t capacity, const char *value) {
    if (value) snprintf(target, capacity, "%s", value);
}
static BongoCatNeoTheme parse_theme(const char *value) {
    if (value && strcmp(value, "light") == 0) return BONGO_CAT_NEO_THEME_LIGHT;
    if (value && strcmp(value, "dark") == 0) return BONGO_CAT_NEO_THEME_DARK;
    return BONGO_CAT_NEO_THEME_AUTO;
}
static BongoCatNeoLanguage parse_language(const char *value) {
    const char *names[] = {"en-US", "zh-CN", "zh-TW", "pt-BR", "vi-VN"};
    if (value) for (int i = 0; i <= BONGO_CAT_NEO_LANG_VI_VN; ++i)
        if (strcmp(value, names[i]) == 0) return (BongoCatNeoLanguage)i;
    return BONGO_CAT_NEO_LANG_EN_US;
}

static void read_model(yyjson_val *obj, BongoCatNeoModelOptions *value) {
    if (!yyjson_is_obj(obj)) return;
    value->mirror = get_bool(obj, "mirror", value->mirror);
    value->mouse_mirror = get_bool(obj, "mouseMirror", value->mouse_mirror);
    value->motion_sound = get_bool(obj, "motionSound", value->motion_sound);
    value->behavior = get_bool(obj, "behavior", value->behavior);
    value->ignore_mouse = get_bool(obj, "ignoreMouse", value->ignore_mouse);
    value->auto_release_seconds = get_float(obj, "autoReleaseDelay", value->auto_release_seconds);
    value->max_fps = get_int(obj, "maxFPS", value->max_fps);
}
static void read_window(yyjson_val *obj, BongoCatNeoWindowOptions *value) {
    if (!yyjson_is_obj(obj)) return;
    value->pass_through = get_bool(obj, "passThrough", value->pass_through);
    value->always_on_top = get_bool(obj, "alwaysOnTop", value->always_on_top);
    value->taskbar_visible = get_bool(obj, "taskbarVisible", value->taskbar_visible);
    value->hide_on_hover = get_bool(obj, "hideOnHover", value->hide_on_hover);
    value->keep_in_screen = get_bool(obj, "keepInScreen", value->keep_in_screen);
    value->hide_delay_seconds = get_float(obj, "hideOnHoverDelay", value->hide_delay_seconds);
}
static void read_app(yyjson_val *obj, BongoCatNeoAppOptions *value) {
    if (!yyjson_is_obj(obj)) return;
    value->autostart = get_bool(obj, "autostart", value->autostart);
    value->tray_visible = get_bool(obj, "trayVisible", value->tray_visible);
    value->theme = parse_theme(get_string(obj, "theme"));
    value->language = parse_language(get_string(obj, "language"));
}
static void read_shortcuts(yyjson_val *obj, BongoCatNeoShortcutOptions *value) {
    if (!yyjson_is_obj(obj)) return;
    copy_string(value->visible_cat, sizeof(value->visible_cat), get_string(obj, "visibleCat"));
    copy_string(value->visible_preferences, sizeof(value->visible_preferences),
        get_string(obj, "visiblePreference"));
    copy_string(value->mirror, sizeof(value->mirror), get_string(obj, "mirrorMode"));
    copy_string(value->pass_through, sizeof(value->pass_through), get_string(obj, "penetrable"));
    copy_string(value->always_on_top, sizeof(value->always_on_top), get_string(obj, "alwaysOnTop"));
}
static void read_behaviors(yyjson_val *array, BongoCatNeoConfig *config) {
    if (!yyjson_is_arr(array)) return;
    config->behavior_shortcut_count = 0;
    size_t index, count; yyjson_val *item;
    yyjson_arr_foreach(array, index, count, item) {
        const char *id = get_string(item, "id"), *shortcut = get_string(item, "shortcut");
        if (!id || !shortcut || config->behavior_shortcut_count >= BONGO_CAT_NEO_BEHAVIOR_CAP)
            continue;
        BongoCatNeoBehaviorShortcut *entry =
            &config->behavior_shortcuts[config->behavior_shortcut_count++];
        copy_string(entry->id, sizeof(entry->id), id);
        copy_string(entry->shortcut, sizeof(entry->shortcut), shortcut);
    }
}

BongoCatNeoResult bongo_cat_neo_preferences_load(const char *path,
    BongoCatNeoConfig *config, BongoCatNeoError *error) {
    if (!path || !config) return BONGO_CAT_NEO_ERROR_ARGUMENT;
    if (!bongo_cat_neo_path_is_file(path)) return BONGO_CAT_NEO_OK;
    yyjson_doc *document = bongo_cat_neo_config_read_document(path, PREFERENCES_FORMAT, error);
    if (!document) return BONGO_CAT_NEO_ERROR_FORMAT;
    BongoCatNeoConfig loaded = *config;
    yyjson_val *root = yyjson_doc_get_root(document);
    read_model(yyjson_obj_get(root, "model"), &loaded.model);
    read_window(yyjson_obj_get(root, "window"), &loaded.window);
    read_app(yyjson_obj_get(root, "app"), &loaded.app);
    read_shortcuts(yyjson_obj_get(root, "shortcuts"), &loaded.shortcuts);
    read_behaviors(yyjson_obj_get(root, "behaviorShortcuts"), &loaded);
    yyjson_doc_free(document);
    bongo_cat_neo_config_validate(&loaded);
    *config = loaded;
    return BONGO_CAT_NEO_OK;
}

static void write_model(yyjson_mut_doc *doc, yyjson_mut_val *obj,
    const BongoCatNeoModelOptions *v) {
    yyjson_mut_obj_add_bool(doc, obj, "mirror", v->mirror);
    yyjson_mut_obj_add_bool(doc, obj, "mouseMirror", v->mouse_mirror);
    yyjson_mut_obj_add_bool(doc, obj, "motionSound", v->motion_sound);
    yyjson_mut_obj_add_bool(doc, obj, "behavior", v->behavior);
    yyjson_mut_obj_add_bool(doc, obj, "ignoreMouse", v->ignore_mouse);
    yyjson_mut_obj_add_real(doc, obj, "autoReleaseDelay", v->auto_release_seconds);
    yyjson_mut_obj_add_int(doc, obj, "maxFPS", v->max_fps);
}
static void write_window(yyjson_mut_doc *doc, yyjson_mut_val *obj,
    const BongoCatNeoWindowOptions *v) {
    yyjson_mut_obj_add_bool(doc, obj, "passThrough", v->pass_through);
    yyjson_mut_obj_add_bool(doc, obj, "alwaysOnTop", v->always_on_top);
    yyjson_mut_obj_add_bool(doc, obj, "taskbarVisible", v->taskbar_visible);
    yyjson_mut_obj_add_bool(doc, obj, "hideOnHover", v->hide_on_hover);
    yyjson_mut_obj_add_bool(doc, obj, "keepInScreen", v->keep_in_screen);
    yyjson_mut_obj_add_real(doc, obj, "hideOnHoverDelay", v->hide_delay_seconds);
}
static void write_app(yyjson_mut_doc *doc, yyjson_mut_val *obj,
    const BongoCatNeoAppOptions *v) {
    yyjson_mut_obj_add_bool(doc, obj, "autostart", v->autostart);
    yyjson_mut_obj_add_bool(doc, obj, "trayVisible", v->tray_visible);
    yyjson_mut_obj_add_strcpy(doc, obj, "theme", bongo_cat_neo_theme_name(v->theme));
    yyjson_mut_obj_add_strcpy(doc, obj, "language", bongo_cat_neo_language_name(v->language));
}
static void write_shortcuts(yyjson_mut_doc *doc, yyjson_mut_val *obj,
    const BongoCatNeoShortcutOptions *v) {
    yyjson_mut_obj_add_strcpy(doc, obj, "visibleCat", v->visible_cat);
    yyjson_mut_obj_add_strcpy(doc, obj, "visiblePreference", v->visible_preferences);
    yyjson_mut_obj_add_strcpy(doc, obj, "mirrorMode", v->mirror);
    yyjson_mut_obj_add_strcpy(doc, obj, "penetrable", v->pass_through);
    yyjson_mut_obj_add_strcpy(doc, obj, "alwaysOnTop", v->always_on_top);
}
static void write_behaviors(yyjson_mut_doc *doc, yyjson_mut_val *root,
    const BongoCatNeoConfig *config) {
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "behaviorShortcuts", array);
    for (size_t i = 0; i < config->behavior_shortcut_count; ++i) {
        const BongoCatNeoBehaviorShortcut *value = &config->behavior_shortcuts[i];
        if (!value->id[0] || !value->shortcut[0]) continue;
        yyjson_mut_val *item = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_strcpy(doc, item, "id", value->id);
        yyjson_mut_obj_add_strcpy(doc, item, "shortcut", value->shortcut);
        yyjson_mut_arr_add_val(array, item);
    }
}

BongoCatNeoResult bongo_cat_neo_preferences_save(const char *path,
    const BongoCatNeoConfig *config, BongoCatNeoError *error) {
    if (!path || !config) return BONGO_CAT_NEO_ERROR_ARGUMENT;
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) return BONGO_CAT_NEO_ERROR_MEMORY;
    yyjson_mut_val *root = yyjson_mut_obj(doc); yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_strcpy(doc, root, "format", PREFERENCES_FORMAT);
    yyjson_mut_obj_add_int(doc, root, "version", 1);
    write_model(doc, yyjson_mut_obj_add_obj(doc, root, "model"), &config->model);
    write_window(doc, yyjson_mut_obj_add_obj(doc, root, "window"), &config->window);
    write_app(doc, yyjson_mut_obj_add_obj(doc, root, "app"), &config->app);
    write_shortcuts(doc, yyjson_mut_obj_add_obj(doc, root, "shortcuts"), &config->shortcuts);
    write_behaviors(doc, root, config);
    BongoCatNeoResult result = bongo_cat_neo_config_write_document(path, doc,
        "preferences file", error);
    yyjson_mut_doc_free(doc);
    return result;
}
