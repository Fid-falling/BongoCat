#include "model_import_tauri_internal.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int key_code(const char *filename) {
    char name[BONGO_CAT_ID_CAP];
    snprintf(name, sizeof(name), "%s", filename ? filename : "");
    char *dot = strrchr(name, '.');
    if (!dot || SDL_strcasecmp(dot, ".png") != 0) return -1;
    *dot = '\0';
    char *end = NULL;
    long numeric = strtol(name, &end, 10);
    if (name[0] && end && !end[0] && numeric >= 0 && numeric <= 255)
        return (int)numeric;
    if (strlen(name) == 4 && SDL_strncasecmp(name, "Key", 3) == 0 &&
        name[3] >= 'A' && name[3] <= 'Z') return name[3];
    if (strlen(name) == 4 && SDL_strncasecmp(name, "Num", 3) == 0 &&
        name[3] >= '0' && name[3] <= '9') return name[3];
    if ((name[0] == 'F' || name[0] == 'f') && strlen(name) <= 3) {
        int value = atoi(name + 1);
        if (value >= 1 && value <= 12) return 111 + value;
    }
    static const struct { const char *name; int code; } map[] = {
        {"Backspace", 8}, {"BackSpace", 8}, {"Tab", 9}, {"Return", 13},
        {"Pause", 19}, {"CapsLock", 20}, {"Escape", 27}, {"Space", 32},
        {"PageUp", 33}, {"PageDown", 34}, {"End", 35}, {"Home", 36},
        {"LeftArrow", 37}, {"UpArrow", 38}, {"RightArrow", 39},
        {"DownArrow", 40}, {"PrintScreen", 44}, {"Insert", 45},
        {"Delete", 46}, {"Meta", 91}, {"MetaLeft", 91}, {"MetaRight", 92},
        {"Apps", 93}, {"Kp0", 96}, {"Kp1", 97}, {"Kp2", 98},
        {"Kp3", 99}, {"Kp4", 100}, {"Kp5", 101}, {"Kp6", 102},
        {"Kp7", 103}, {"Kp8", 104}, {"Kp9", 105},
        {"KpMultiply", 106}, {"KpPlus", 107}, {"KpMinus", 109},
        {"KpDecimal", 110}, {"KpDivide", 111}, {"NumLock", 144},
        {"ScrollLock", 145}, {"Semicolon", 186}, {"SemiColon", 186},
        {"Equal", 187}, {"Comma", 188}, {"Minus", 189},
        {"Period", 190}, {"Dot", 190}, {"Slash", 191},
        {"BackQuote", 192}, {"BracketLeft", 219}, {"LeftBracket", 219},
        {"Backslash", 220}, {"BracketRight", 221}, {"RightBracket", 221},
        {"Quote", 222}, {"Shift", 16}, {"ShiftLeft", 16},
        {"ShiftRight", 16}, {"Control", 17}, {"ControlLeft", 17},
        {"ControlRight", 17}, {"Alt", 18}, {"AltGr", 18}
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); ++i)
        if (SDL_strcasecmp(name, map[i].name) == 0) return map[i].code;
    static const struct { const char *name; int code; } gamepad[] = {
        {"South", 0}, {"East", 1}, {"West", 2}, {"North", 3},
        {"LeftTrigger", 4}, {"RightTrigger", 5}, {"LeftTrigger2", 6},
        {"RightTrigger2", 7}, {"LeftThumb", 8}, {"RightThumb", 9},
        {"DPadLeft", 10}, {"DPadRight", 11}, {"DPadUp", 12},
        {"DPadDown", 13}, {"Start", 14}, {"Select", 15}
    };
    for (size_t i = 0; i < sizeof(gamepad) / sizeof(gamepad[0]); ++i)
        if (SDL_strcasecmp(name, gamepad[i].name) == 0) return gamepad[i].code;
    return -1;
}

static BongoCatPathVisit collect_keys(void *userdata,
    const char *dirname, const char *name) {
    TauriKeyFiles *files = userdata;
    if (files->count >= TAURI_KEY_CAP ||
        !bongo_cat_import_has_suffix_ci(name, ".png"))
        return BONGO_CAT_PATH_CONTINUE;
    int code = key_code(name);
    if (code < 0) return BONGO_CAT_PATH_CONTINUE;
    TauriKeyFile *item = &files->values[files->count];
    if (!bongo_cat_path_join(item->path, sizeof(item->path), dirname, name))
        return BONGO_CAT_PATH_FAILURE;
    if (!bongo_cat_path_is_file(item->path)) return BONGO_CAT_PATH_CONTINUE;
    snprintf(item->name, sizeof(item->name), "%s", name);
    item->code = code;
    files->count++;
    return BONGO_CAT_PATH_CONTINUE;
}

static int compare_keys(const void *left, const void *right) {
    const TauriKeyFile *a = left, *b = right;
    return SDL_strcasecmp(a->name, b->name);
}

static bool copy_keys(const BongoCatImportCandidate *candidate,
    const char *resource_directory, const char *mode_root,
    const char *source_group, const char *target_group,
    TauriKeyFiles *files, BongoCatError *error) {
    char source[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(source, sizeof(source), resource_directory,
            source_group)) return false;
    if (!bongo_cat_path_is_dir(source)) {
        char resources[BONGO_CAT_PATH_CAP];
        if (!bongo_cat_path_join(resources, sizeof(resources),
                candidate->directory, "resources") ||
            !bongo_cat_path_join(source, sizeof(source), resources,
                source_group) || !bongo_cat_path_is_dir(source)) return true;
    }
    if (!bongo_cat_path_enumerate(source, collect_keys, files)) return false;
    qsort(files->values, files->count, sizeof(files->values[0]), compare_keys);
    char destination[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(destination, sizeof(destination), mode_root,
            target_group) || !bongo_cat_path_create_directory(destination))
        return false;
    for (size_t i = 0; i < files->count; ++i) {
        char filename[32], path[BONGO_CAT_PATH_CAP];
        snprintf(filename, sizeof(filename), "%zu.png", i);
        if (!bongo_cat_path_join(path, sizeof(path), destination, filename) ||
            !bongo_cat_path_copy_file(files->values[i].path, path)) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
                "Cannot copy Tauri input image: %s", files->values[i].path);
            return false;
        }
    }
    return true;
}

static bool ensure_key(const BongoCatImportCandidate *candidate,
    const char *mode_root, const char *target_group, int fallback_code,
    TauriKeyFiles *files, BongoCatError *error) {
    if (files->count) return true;
    char source[BONGO_CAT_PATH_CAP];
    bool have_source = bongo_cat_tauri_find_resource_file(candidate,
        "cover.png", source, sizeof(source)) ||
        bongo_cat_tauri_find_resource_file(candidate, "background.png",
            source, sizeof(source));
    char group[BONGO_CAT_PATH_CAP], target[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(group, sizeof(group), mode_root, target_group) ||
        !bongo_cat_path_create_directory(group) ||
        !bongo_cat_path_join(target, sizeof(target), group, "0.png") ||
        !bongo_cat_tauri_copy_image_or_placeholder(
            have_source ? source : NULL, target, error)) return false;
    files->values[0].code = fallback_code;
    files->count = 1;
    return true;
}

bool bongo_cat_tauri_copy_input_images(
    const BongoCatImportCandidate *candidate, const char *resource_directory,
    const char *mode_root, TauriKeyFiles *left, TauriKeyFiles *right,
    BongoCatError *error) {
    *left = (TauriKeyFiles){0};
    *right = (TauriKeyFiles){0};
    if (candidate->mode == BONGO_CAT_MODE_STANDARD) {
        return copy_keys(candidate, resource_directory,
            mode_root, "left-keys", "hand", left, error) &&
            ensure_key(candidate, mode_root, "hand", 65,
                left, error);
    }
    return copy_keys(candidate, resource_directory, mode_root,
        "left-keys", "lefthand", left, error) &&
        copy_keys(candidate, resource_directory, mode_root,
            "right-keys", "righthand", right, error) &&
        ensure_key(candidate, mode_root, "lefthand",
            candidate->mode == BONGO_CAT_MODE_GAMEPAD ? 0 : 65, left, error) &&
        ensure_key(candidate, mode_root, "righthand",
            candidate->mode == BONGO_CAT_MODE_GAMEPAD ? 1 : 65, right, error);
}
