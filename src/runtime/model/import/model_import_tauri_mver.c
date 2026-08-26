#include "model_import.h"
#include "bongo_cat/file.h"
#include "bongo_cat/json.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

#define TAURI_KEY_CAP 256
#define TAURI_MODEL_COPY_DEPTH_CAP 32

typedef struct TauriKeyFile {
    char name[BONGO_CAT_ID_CAP];
    char path[BONGO_CAT_PATH_CAP];
    int code;
} TauriKeyFile;

typedef struct TauriKeyFiles {
    TauriKeyFile values[TAURI_KEY_CAP];
    size_t count;
} TauriKeyFiles;

static bool suffix(const char *name, const char *ending) {
    size_t a = name ? strlen(name) : 0, b = ending ? strlen(ending) : 0;
    return a >= b && SDL_strcasecmp(name + a - b, ending) == 0;
}

static int key_code(const char *filename) {
    char name[BONGO_CAT_ID_CAP];
    snprintf(name, sizeof(name), "%s", filename ? filename : "");
    char *dot = strrchr(name, '.');
    if (!dot || SDL_strcasecmp(dot, ".png") != 0) return -1;
    *dot = '\0';
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

static BongoCatPathVisit collect_keys(void *userdata, const char *dirname,
    const char *name) {
    TauriKeyFiles *files = userdata;
    if (files->count >= TAURI_KEY_CAP || !suffix(name, ".png"))
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

static bool resource_file(const BongoCatImportCandidate *candidate,
    const char *name, char *path, size_t capacity) {
    const char *roots[3] = {candidate->assets, candidate->directory,
        candidate->package_root};
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i) {
        if (!roots[i][0]) continue;
        char resources[BONGO_CAT_PATH_CAP];
        if (bongo_cat_path_join(resources, sizeof(resources), roots[i],
                "resources") && bongo_cat_path_join(path, capacity,
                resources, name) && bongo_cat_path_is_file(path)) return true;
        if (bongo_cat_path_join(path, capacity, roots[i], name) &&
            bongo_cat_path_is_file(path)) return true;
    }
    return false;
}

static bool write_placeholder(const char *path) {
    static const unsigned char png[] = {
        0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,
        0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,
        0x08,0x06,0x00,0x00,0x00,0x1f,0x15,0xc4,0x89,0x00,0x00,0x00,
        0x0d,0x49,0x44,0x41,0x54,0x08,0xd7,0x63,0xf8,0xcf,0xc0,0xf0,
        0x1f,0x00,0x05,0x00,0x01,0xff,0x89,0x99,0x3d,0x1d,0x00,0x00,
        0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82
    };
    FILE *file = bongo_cat_file_open(path, "wb");
    if (!file) return false;
    bool ok = fwrite(png, 1, sizeof(png), file) == sizeof(png);
    if (fclose(file) != 0) ok = false;
    return ok;
}

static bool copy_image_or_placeholder(const char *source, const char *target,
    BongoCatError *error) {
    bool ok = source && bongo_cat_path_is_file(source)
        ? bongo_cat_path_copy_file(source, target) : write_placeholder(target);
    if (!ok && error) bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
        "Cannot create normalized Mver image: %s", target);
    return ok;
}

typedef struct TauriModelCopy {
    const char *target_root;
    BongoCatError *error;
    unsigned depth;
} TauriModelCopy;

static bool copy_tauri_model_tree(const char *source, const char *target,
    unsigned depth, BongoCatError *error);
static bool copy_tauri_resource_tree(const char *source, const char *target,
    unsigned depth, BongoCatError *error);

static bool tauri_interface_resource(const char *name) {
    static const char *const files[] = {
        "cover.png", "background.png", "cat.png", "bg.png",
        "mousebg.png", "tabletbg.png"
    };
    if (SDL_strcasecmp(name, "left-keys") == 0 ||
        SDL_strcasecmp(name, "right-keys") == 0) return true;
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i)
        if (SDL_strcasecmp(name, files[i]) == 0) return true;
    return false;
}

static BongoCatPathVisit copy_tauri_resource_child(void *userdata,
    const char *dirname, const char *name) {
    TauriModelCopy *context = userdata;
    if (!name || name[0] == '.' || tauri_interface_resource(name))
        return BONGO_CAT_PATH_CONTINUE;
    char source[BONGO_CAT_PATH_CAP], target[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(source, sizeof(source), dirname, name) ||
        !bongo_cat_path_join(target, sizeof(target), context->target_root,
            name)) return BONGO_CAT_PATH_FAILURE;
    if (bongo_cat_path_is_dir(source))
        return copy_tauri_model_tree(source, target, context->depth + 1,
            context->error) ? BONGO_CAT_PATH_CONTINUE : BONGO_CAT_PATH_FAILURE;
    if (!bongo_cat_path_is_file(source)) return BONGO_CAT_PATH_CONTINUE;
    if (!bongo_cat_path_create_directory(context->target_root) ||
        !bongo_cat_path_copy_file(source, target)) {
        bongo_cat_error_set(context->error, BONGO_CAT_ERROR_IO,
            "Cannot preserve Tauri Live2D resource: %s", source);
        return BONGO_CAT_PATH_FAILURE;
    }
    return BONGO_CAT_PATH_CONTINUE;
}

static BongoCatPathVisit copy_tauri_model_child(void *userdata,
    const char *dirname, const char *name) {
    TauriModelCopy *context = userdata;
    if (!name || name[0] == '.') return BONGO_CAT_PATH_CONTINUE;
    char source[BONGO_CAT_PATH_CAP], target[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(source, sizeof(source), dirname, name) ||
        !bongo_cat_path_join(target, sizeof(target), context->target_root,
            name)) return BONGO_CAT_PATH_FAILURE;
    /* Normalize Tauri interface assets, but retain model-owned resources. */
    if (context->depth == 0 && bongo_cat_path_is_dir(source) &&
        SDL_strcasecmp(name, "resources") == 0)
        return copy_tauri_resource_tree(source, target, context->depth + 1,
            context->error) ? BONGO_CAT_PATH_CONTINUE : BONGO_CAT_PATH_FAILURE;
    if (bongo_cat_path_is_dir(source))
        return copy_tauri_model_tree(source, target, context->depth + 1,
            context->error) ? BONGO_CAT_PATH_CONTINUE : BONGO_CAT_PATH_FAILURE;
    if (!bongo_cat_path_is_file(source) ||
        !bongo_cat_path_copy_file(source, target)) {
        bongo_cat_error_set(context->error, BONGO_CAT_ERROR_IO,
            "Cannot copy Tauri Live2D asset: %s", source);
        return BONGO_CAT_PATH_FAILURE;
    }
    return BONGO_CAT_PATH_CONTINUE;
}

static bool copy_tauri_model_tree(const char *source, const char *target,
    unsigned depth, BongoCatError *error) {
    if (depth > TAURI_MODEL_COPY_DEPTH_CAP ||
        !bongo_cat_path_create_directory(target) ||
        !bongo_cat_path_enumerate(source, copy_tauri_model_child,
            &(TauriModelCopy){target, error, depth})) {
        if (error && !error->message[0]) bongo_cat_error_set(error,
            BONGO_CAT_ERROR_IO, "Cannot copy Tauri Live2D directory: %s",
            source);
        return false;
    }
    return true;
}

static bool copy_tauri_resource_tree(const char *source, const char *target,
    unsigned depth, BongoCatError *error) {
    if (depth > TAURI_MODEL_COPY_DEPTH_CAP ||
        !bongo_cat_path_enumerate(source, copy_tauri_resource_child,
            &(TauriModelCopy){target, error, depth})) {
        if (error && !error->message[0]) bongo_cat_error_set(error,
            BONGO_CAT_ERROR_IO, "Cannot inspect Tauri resource directory: %s",
            source);
        return false;
    }
    return true;
}

static bool add_matrix(yyjson_mut_doc *document, yyjson_mut_val *mode,
    const char *name, const TauriKeyFiles *files) {
    yyjson_mut_val *matrix = yyjson_mut_obj_add_arr(document, mode, name);
    if (!matrix) return false;
    for (size_t i = 0; i < files->count; ++i) {
        yyjson_mut_val *row = yyjson_mut_arr_add_arr(document, matrix);
        if (!row || !yyjson_mut_arr_add_int(document, row, files->values[i].code))
            return false;
    }
    return true;
}

static bool config_write(const char *path, BongoCatModelMode mode,
    const TauriKeyFiles *left, const TauriKeyFiles *right,
    BongoCatError *error) {
    yyjson_mut_doc *document = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = document ? yyjson_mut_obj(document) : NULL;
    if (document) yyjson_mut_doc_set_root(document, root);
    yyjson_mut_val *decoration = root ? yyjson_mut_obj_add_obj(document, root,
        "decoration") : NULL;
    yyjson_mut_val *mode_object = root ? yyjson_mut_obj_add_obj(document, root,
        bongo_cat_mode_name(mode)) : NULL;
    bool ok = root && decoration && mode_object &&
        yyjson_mut_obj_add_int(document, root, "mode",
            mode == BONGO_CAT_MODE_STANDARD ? 1 :
            mode == BONGO_CAT_MODE_KEYBOARD ? 2 : 3) &&
        yyjson_mut_obj_add_bool(document, mode_object, "l2d", true);
    if (ok && mode == BONGO_CAT_MODE_STANDARD)
        ok = add_matrix(document, mode_object, "hand", left) &&
            yyjson_mut_obj_add_bool(document, mode_object, "mouse", false) &&
            yyjson_mut_obj_add_arr(document, mode_object, "keyboard");
    if (ok && mode != BONGO_CAT_MODE_STANDARD)
        ok = add_matrix(document, mode_object, "lefthand", left) &&
            add_matrix(document, mode_object, "righthand", right) &&
            yyjson_mut_obj_add_arr(document, mode_object, "keyboard");
    if (ok && mode == BONGO_CAT_MODE_GAMEPAD)
        ok = yyjson_mut_obj_add_int(document, mode_object, "input_mode", 1);
    if (ok) ok = yyjson_mut_obj_add_arr(document, mode_object, "face") &&
        yyjson_mut_obj_add_arr(document, mode_object, "sounds") &&
        yyjson_mut_obj_add_arr(document, mode_object, "l2d_expression") &&
        yyjson_mut_obj_add_arr(document, mode_object, "l2d_motion") &&
        yyjson_mut_obj_add_arr(document, mode_object, "l2d_motion_lockhand");
    if (ok) ok = bongo_cat_json_write_file(path, document, YYJSON_WRITE_PRETTY,
        NULL);
    yyjson_mut_doc_free(document);
    if (!ok && error) bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
        "Cannot write normalized Mver configuration: %s", path);
    return ok;
}

static bool copy_preview(const BongoCatImportCandidate *candidate,
    const char *mode_root, BongoCatError *error) {
    char source[BONGO_CAT_PATH_CAP], cat[BONGO_CAT_PATH_CAP];
    char target[BONGO_CAT_PATH_CAP];
    bool have_cover = resource_file(candidate, "cover.png", source,
        sizeof(source));
    if (!bongo_cat_path_join(cat, sizeof(cat), mode_root, "cat.png") ||
        !copy_image_or_placeholder(have_cover ? source : NULL, cat, error))
        return false;
    bool have_background = resource_file(candidate, "background.png", source,
        sizeof(source));
    if (!have_background && have_cover) {
        /* A cover is a better fallback than an absent preview. */
        snprintf(source, sizeof(source), "%s", cat);
        have_background = true;
    }
    if (!bongo_cat_path_join(target, sizeof(target), mode_root, "bg.png"))
        return false;
    return copy_image_or_placeholder(have_background ? source : NULL, target,
        error);
}

static bool copy_keys(const BongoCatImportCandidate *candidate,
    const char *resource_directory, const char *mode_root, const char *name,
    const char *mver_group, TauriKeyFiles *files, BongoCatError *error) {
    char source[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(source, sizeof(source), resource_directory, name))
        return false;
    if (!bongo_cat_path_is_dir(source)) {
        char parent[BONGO_CAT_PATH_CAP];
        if (!bongo_cat_path_join(parent, sizeof(parent), candidate->directory,
                "resources") || !bongo_cat_path_join(source, sizeof(source),
                parent, name) || !bongo_cat_path_is_dir(source)) return true;
    }
    if (!bongo_cat_path_enumerate(source, collect_keys, files)) return false;
    qsort(files->values, files->count, sizeof(files->values[0]), compare_keys);
    char destination[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(destination, sizeof(destination), mode_root,
            mver_group) || !bongo_cat_path_create_directory(destination))
        return false;
    for (size_t i = 0; i < files->count; ++i) {
        char filename[32], path[BONGO_CAT_PATH_CAP];
        snprintf(filename, sizeof(filename), "%zu.png", i);
        if (!bongo_cat_path_join(path, sizeof(path), destination, filename) ||
            !bongo_cat_path_copy_file(files->values[i].path, path)) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
                "Cannot copy tauri input image: %s", files->values[i].path);
            return false;
        }
    }
    return true;
}

bool bongo_cat_import_tauri_convert_to_mver(
    const BongoCatImportCandidate *candidate, const char *target,
    BongoCatImportCandidate *installed, BongoCatError *error) {
    if (!candidate || !target || !installed ||
        candidate->format != BONGO_CAT_IMPORT_TAURI) return false;
    char img[BONGO_CAT_PATH_CAP], mode_root[BONGO_CAT_PATH_CAP], model_root[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_create_directory(target) ||
        !bongo_cat_path_join(img, sizeof(img), target, "img") ||
        !bongo_cat_path_join(mode_root, sizeof(mode_root), img,
            bongo_cat_mode_name(candidate->mode)) ||
        !bongo_cat_path_join(model_root, sizeof(model_root), mode_root,
            "cat_model") || !bongo_cat_path_create_directory(mode_root) ||
        !bongo_cat_path_create_directory(model_root)) return false;
    if (!copy_tauri_model_tree(candidate->directory, model_root, 0, error))
        return false;
    char resource_root[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(resource_root, sizeof(resource_root),
            candidate->assets, "resources") || !bongo_cat_path_is_dir(resource_root)) {
        if (!bongo_cat_path_join(resource_root, sizeof(resource_root),
                candidate->directory, "resources") ||
            !bongo_cat_path_is_dir(resource_root))
            snprintf(resource_root, sizeof(resource_root), "%s",
                candidate->assets);
    }
    TauriKeyFiles left = {0}, right = {0};
    if (candidate->mode == BONGO_CAT_MODE_STANDARD) {
        if (!copy_keys(candidate, resource_root, mode_root, "left-keys", "hand",
                &left, error)) return false;
    } else if (!copy_keys(candidate, resource_root, mode_root, "left-keys",
            "lefthand", &left, error) ||
        !copy_keys(candidate, resource_root, mode_root, "right-keys", "righthand",
            &right, error)) return false;
    char source[BONGO_CAT_PATH_CAP], fallback[BONGO_CAT_PATH_CAP];
    bool have_fallback = resource_file(candidate, "cover.png", source,
        sizeof(source)) || resource_file(candidate, "background.png", source,
        sizeof(source));
    if (!left.count) {
        char hand[BONGO_CAT_PATH_CAP];
        if (!bongo_cat_path_join(hand, sizeof(hand), mode_root,
                candidate->mode == BONGO_CAT_MODE_STANDARD ? "hand" : "lefthand") ||
            !bongo_cat_path_create_directory(hand) ||
            !bongo_cat_path_join(fallback, sizeof(fallback), hand, "0.png") ||
            !copy_image_or_placeholder(have_fallback ? source : NULL, fallback,
                error)) return false;
        left.values[0].code = candidate->mode == BONGO_CAT_MODE_GAMEPAD ? 0 : 65;
        left.count = 1;
    }
    if (candidate->mode != BONGO_CAT_MODE_STANDARD && !right.count) {
        char hand[BONGO_CAT_PATH_CAP];
        if (!bongo_cat_path_join(hand, sizeof(hand), mode_root, "righthand") ||
            !bongo_cat_path_create_directory(hand) ||
            !bongo_cat_path_join(fallback, sizeof(fallback), hand, "0.png") ||
            !copy_image_or_placeholder(have_fallback ? source : NULL, fallback,
                error)) return false;
        right.values[0].code = candidate->mode == BONGO_CAT_MODE_GAMEPAD ? 1 : 65;
        right.count = 1;
    }
    char config[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(config, sizeof(config), target, "config.json") ||
        !config_write(config, candidate->mode, &left, &right, error) ||
        !copy_preview(candidate, mode_root, error)) return false;
    *installed = *candidate;
    snprintf(installed->directory, sizeof(installed->directory), "%s", model_root);
    snprintf(installed->assets, sizeof(installed->assets), "%s", mode_root);
    snprintf(installed->package_root, sizeof(installed->package_root), "%s", target);
    snprintf(installed->config, sizeof(installed->config), "%s", config);
    installed->overrides[0] = '\0';
    installed->patch_root[0] = '\0';
    installed->format = BONGO_CAT_IMPORT_MVER;
    installed->gamepad_buttons = candidate->mode == BONGO_CAT_MODE_GAMEPAD;
    return true;
}
