#include "config_internal.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SETTINGS_FORMAT "bongocat/settings"

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
static BongoCatTheme parse_theme(const char *value) {
    if (value && strcmp(value, "light") == 0) return BONGO_CAT_THEME_LIGHT;
    if (value && strcmp(value, "dark") == 0) return BONGO_CAT_THEME_DARK;
    return BONGO_CAT_THEME_AUTO;
}
static BongoCatLanguage parse_language(const char *value) {
    if (value) for (int i = 0; i < BONGO_CAT_LANG_COUNT; ++i)
        if (strcmp(value, bongo_cat_language_name((BongoCatLanguage)i)) == 0)
            return (BongoCatLanguage)i;
    return BONGO_CAT_LANG_EN_US;
}
static BongoCatObsBackgroundColor parse_obs_background_color(
    const char *value, BongoCatObsBackgroundColor fallback) {
    if (value) for (int i = 0; i < BONGO_CAT_OBS_BACKGROUND_COLOR_COUNT; ++i)
        if (strcmp(value, bongo_cat_obs_background_color_name(
            (BongoCatObsBackgroundColor)i)) == 0)
            return (BongoCatObsBackgroundColor)i;
    return fallback;
}

static void read_model(yyjson_val *obj, BongoCatModelPreferences *value) {
    if (!yyjson_is_obj(obj)) return;
    value->mirror = get_bool(obj, "modelMirrored", value->mirror);
    value->mouse_mirror = get_bool(obj, "pointerMirrored", value->mouse_mirror);
    value->mouse_centered = get_bool(obj, "centerPointerTracking", value->mouse_centered);
    value->ignore_mouse = get_bool(obj, "ignorePointerInput", value->ignore_mouse);
    value->auto_release_seconds = get_float(obj, "inputReleaseDelaySeconds",
        value->auto_release_seconds);
    value->max_fps = get_int(obj, "maximumFps", value->max_fps);
}
static void read_window(yyjson_val *obj, BongoCatWindowPreferences *value) {
    if (!yyjson_is_obj(obj)) return;
    value->pass_through = get_bool(obj, "clickThrough", value->pass_through);
    value->always_on_top = get_bool(obj, "alwaysOnTop", value->always_on_top);
    value->hide_on_hover = get_bool(obj, "hideOnPointerOver", value->hide_on_hover);
    value->keep_in_screen = get_bool(obj, "keepOnScreen", value->keep_in_screen);
    value->obs_background = get_bool(obj, "captureBackground", value->obs_background);
    value->obs_background_color = parse_obs_background_color(
        get_string(obj, "captureBackgroundColor"), value->obs_background_color);
    value->hide_delay_seconds = get_float(obj, "hideDelaySeconds",
        value->hide_delay_seconds);
}
static void read_app(yyjson_val *obj, BongoCatApplicationPreferences *value) {
    if (!yyjson_is_obj(obj)) return;
    value->autostart = get_bool(obj, "launchAtLogin", value->autostart);
    value->tray_visible = get_bool(obj, "showTrayIcon", value->tray_visible);
    value->theme = parse_theme(get_string(obj, "theme"));
    value->language = parse_language(get_string(obj, "language"));
}
static void read_shortcuts(yyjson_val *obj, BongoCatShortcutPreferences *value) {
    if (!yyjson_is_obj(obj)) return;
    copy_string(value->visible_cat, sizeof(value->visible_cat),
        get_string(obj, "toggleVisibility"));
    copy_string(value->visible_preferences, sizeof(value->visible_preferences),
        get_string(obj, "openSettings"));
    copy_string(value->mirror, sizeof(value->mirror),
        get_string(obj, "toggleModelMirror"));
    copy_string(value->pass_through, sizeof(value->pass_through),
        get_string(obj, "toggleClickThrough"));
    copy_string(value->always_on_top, sizeof(value->always_on_top),
        get_string(obj, "toggleAlwaysOnTop"));
}
static void read_behaviors(yyjson_val *array, BongoCatSettings *config) {
    if (!yyjson_is_arr(array)) return;
    config->behavior_shortcut_count = 0;
    size_t index, count; yyjson_val *item;
    yyjson_arr_foreach(array, index, count, item) {
        const char *id = get_string(item, "behaviorId");
        const char *shortcut = get_string(item, "shortcut");
        const char *label = get_string(item, "displayName");
        if (!id || (!shortcut && !label) ||
            config->behavior_shortcut_count >= BONGO_CAT_BEHAVIOR_BINDING_CAP)
            continue;
        BongoCatBehaviorShortcut *entry =
            &config->behavior_shortcuts[config->behavior_shortcut_count++];
        copy_string(entry->id, sizeof(entry->id), id);
        copy_string(entry->shortcut, sizeof(entry->shortcut), shortcut);
        copy_string(entry->label, sizeof(entry->label), label);
    }
}

static void read_model_labels(yyjson_val *array, BongoCatSettings *config) {
    if (!yyjson_is_arr(array)) return;
    config->model_label_count = 0;
    size_t index, count; yyjson_val *item;
    yyjson_arr_foreach(array, index, count, item) {
        const char *id = get_string(item, "modelId");
        const char *label = get_string(item, "displayName");
        if (!id || !id[0] || !label || !label[0] ||
            config->model_label_count >= BONGO_CAT_MODEL_CAP) continue;
        BongoCatModelLabel *entry =
            &config->model_labels[config->model_label_count++];
        copy_string(entry->id, sizeof(entry->id), id);
        copy_string(entry->label, sizeof(entry->label), label);
    }
}

static BongoCatResult read_extensions(yyjson_val *value,
    BongoCatSettings *settings, BongoCatError *error) {
    if (!value) return BONGO_CAT_OK;
    if (!yyjson_is_obj(value)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Settings extensions must be a JSON object");
        return BONGO_CAT_ERROR_FORMAT;
    }
    size_t length = 0;
    char *json = yyjson_val_write(value, YYJSON_WRITE_NOFLAG, &length);
    if (!json) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Cannot preserve settings extensions");
        return BONGO_CAT_ERROR_MEMORY;
    }
    if (length >= sizeof(settings->extensions_json)) {
        free(json);
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Settings extensions exceed the %u-byte limit",
            (unsigned)(sizeof(settings->extensions_json) - 1));
        return BONGO_CAT_ERROR_FORMAT;
    }
    memset(settings->extensions_json, 0, sizeof(settings->extensions_json));
    memcpy(settings->extensions_json, json, length + 1);
    free(json);
    return BONGO_CAT_OK;
}

BongoCatResult bongo_cat_settings_load(const char *path,
    BongoCatSettings *config, BongoCatError *error) {
    if (!path || !config) return BONGO_CAT_ERROR_ARGUMENT;
    if (!bongo_cat_path_is_file(path)) return BONGO_CAT_OK;
    yyjson_doc *document = bongo_cat_config_read_document(path,
        SETTINGS_FORMAT, BONGO_CAT_SETTINGS_SCHEMA, error);
    if (!document) return BONGO_CAT_ERROR_FORMAT;
    BongoCatSettings loaded = *config;
    yyjson_val *root = yyjson_doc_get_root(document);
    read_model(yyjson_obj_get(root, "rendering"), &loaded.model);
    read_window(yyjson_obj_get(root, "window"), &loaded.window);
    read_app(yyjson_obj_get(root, "application"), &loaded.app);
    read_shortcuts(yyjson_obj_get(root, "shortcuts"), &loaded.shortcuts);
    read_behaviors(yyjson_obj_get(root, "behaviorOverrides"), &loaded);
    read_model_labels(yyjson_obj_get(root, "modelOverrides"), &loaded);
    BongoCatResult extensions = read_extensions(
        yyjson_obj_get(root, "extensions"), &loaded, error);
    yyjson_doc_free(document);
    if (extensions != BONGO_CAT_OK) return extensions;
    bongo_cat_settings_validate(&loaded);
    *config = loaded;
    return BONGO_CAT_OK;
}

static void write_model(yyjson_mut_doc *doc, yyjson_mut_val *obj,
    const BongoCatModelPreferences *v) {
    yyjson_mut_obj_add_bool(doc, obj, "modelMirrored", v->mirror);
    yyjson_mut_obj_add_bool(doc, obj, "pointerMirrored", v->mouse_mirror);
    yyjson_mut_obj_add_bool(doc, obj, "centerPointerTracking", v->mouse_centered);
    yyjson_mut_obj_add_bool(doc, obj, "ignorePointerInput", v->ignore_mouse);
    yyjson_mut_obj_add_real(doc, obj, "inputReleaseDelaySeconds",
        v->auto_release_seconds);
    yyjson_mut_obj_add_int(doc, obj, "maximumFps", v->max_fps);
}
static void write_window(yyjson_mut_doc *doc, yyjson_mut_val *obj,
    const BongoCatWindowPreferences *v) {
    yyjson_mut_obj_add_bool(doc, obj, "clickThrough", v->pass_through);
    yyjson_mut_obj_add_bool(doc, obj, "alwaysOnTop", v->always_on_top);
    yyjson_mut_obj_add_bool(doc, obj, "hideOnPointerOver", v->hide_on_hover);
    yyjson_mut_obj_add_bool(doc, obj, "keepOnScreen", v->keep_in_screen);
    yyjson_mut_obj_add_bool(doc, obj, "captureBackground", v->obs_background);
    yyjson_mut_obj_add_strcpy(doc, obj, "captureBackgroundColor",
        bongo_cat_obs_background_color_name(v->obs_background_color));
    yyjson_mut_obj_add_real(doc, obj, "hideDelaySeconds", v->hide_delay_seconds);
}
static void write_app(yyjson_mut_doc *doc, yyjson_mut_val *obj,
    const BongoCatApplicationPreferences *v) {
    yyjson_mut_obj_add_bool(doc, obj, "launchAtLogin", v->autostart);
    yyjson_mut_obj_add_bool(doc, obj, "showTrayIcon", v->tray_visible);
    yyjson_mut_obj_add_strcpy(doc, obj, "theme", bongo_cat_theme_name(v->theme));
    yyjson_mut_obj_add_strcpy(doc, obj, "language", bongo_cat_language_name(v->language));
}
static void write_shortcuts(yyjson_mut_doc *doc, yyjson_mut_val *obj,
    const BongoCatShortcutPreferences *v) {
    yyjson_mut_obj_add_strcpy(doc, obj, "toggleVisibility", v->visible_cat);
    yyjson_mut_obj_add_strcpy(doc, obj, "openSettings", v->visible_preferences);
    yyjson_mut_obj_add_strcpy(doc, obj, "toggleModelMirror", v->mirror);
    yyjson_mut_obj_add_strcpy(doc, obj, "toggleClickThrough", v->pass_through);
    yyjson_mut_obj_add_strcpy(doc, obj, "toggleAlwaysOnTop", v->always_on_top);
}
static void write_behaviors(yyjson_mut_doc *doc, yyjson_mut_val *root,
    const BongoCatSettings *config) {
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "behaviorOverrides", array);
    for (size_t i = 0; i < config->behavior_shortcut_count; ++i) {
        const BongoCatBehaviorShortcut *value = &config->behavior_shortcuts[i];
        if (!value->id[0] || (!value->shortcut[0] && !value->label[0])) continue;
        yyjson_mut_val *item = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_strcpy(doc, item, "behaviorId", value->id);
        if (value->shortcut[0])
            yyjson_mut_obj_add_strcpy(doc, item, "shortcut", value->shortcut);
        if (value->label[0])
            yyjson_mut_obj_add_strcpy(doc, item, "displayName", value->label);
        yyjson_mut_arr_add_val(array, item);
    }
}

static void write_model_labels(yyjson_mut_doc *doc, yyjson_mut_val *root,
    const BongoCatSettings *config) {
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "modelOverrides", array);
    for (size_t i = 0; i < config->model_label_count; ++i) {
        const BongoCatModelLabel *value = &config->model_labels[i];
        if (!value->id[0] || !value->label[0]) continue;
        yyjson_mut_val *item = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_strcpy(doc, item, "modelId", value->id);
        yyjson_mut_obj_add_strcpy(doc, item, "displayName", value->label);
        yyjson_mut_arr_add_val(array, item);
    }
}

static yyjson_mut_val *write_extensions(yyjson_mut_doc *target,
    const BongoCatSettings *settings, BongoCatError *error) {
    const char *json = settings->extensions_json;
    const char *end = memchr(json, '\0', sizeof(settings->extensions_json));
    if (!end) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Settings extensions are not null-terminated");
        return NULL;
    }
    size_t length = (size_t)(end - json);
    if (!length) { json = "{}"; length = 2; }
    yyjson_doc *source = yyjson_read(json, length, 0);
    yyjson_val *root = source ? yyjson_doc_get_root(source) : NULL;
    yyjson_mut_val *copy = yyjson_is_obj(root)
        ? yyjson_val_mut_copy(target, root) : NULL;
    yyjson_doc_free(source);
    if (!copy) bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
        "Settings extensions are not a valid JSON object");
    return copy;
}

BongoCatResult bongo_cat_settings_save(const char *path,
    const BongoCatSettings *config, BongoCatError *error) {
    if (!path || !config) return BONGO_CAT_ERROR_ARGUMENT;
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) return BONGO_CAT_ERROR_MEMORY;
    yyjson_mut_val *root = yyjson_mut_obj(doc); yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_val *extensions = write_extensions(doc, config, error);
    if (!extensions) {
        yyjson_mut_doc_free(doc);
        return error && error->code ? error->code : BONGO_CAT_ERROR_FORMAT;
    }
    yyjson_mut_obj_add_strcpy(doc, root, "format", SETTINGS_FORMAT);
    yyjson_mut_obj_add_int(doc, root, "schemaVersion",
        BONGO_CAT_SETTINGS_SCHEMA);
    write_model(doc, yyjson_mut_obj_add_obj(doc, root, "rendering"), &config->model);
    write_window(doc, yyjson_mut_obj_add_obj(doc, root, "window"), &config->window);
    write_app(doc, yyjson_mut_obj_add_obj(doc, root, "application"), &config->app);
    write_shortcuts(doc, yyjson_mut_obj_add_obj(doc, root, "shortcuts"), &config->shortcuts);
    write_behaviors(doc, root, config);
    write_model_labels(doc, root, config);
    yyjson_mut_obj_add_val(doc, root, "extensions", extensions);
    BongoCatResult result = bongo_cat_config_write_document(path, doc,
        "settings file", error);
    yyjson_mut_doc_free(doc);
    return result;
}
