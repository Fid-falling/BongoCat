#include "model_import.h"
#include "runtime.h"
#include "bongo_cat/file.h"
#include "bongo_cat/image.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

static bool safe_reference(const char *value) {
    if (!value || !value[0] || value[0] == '/' || value[0] == '\\' ||
        strchr(value, ':')) return false;
    const char *part = value;
    while (*part) {
        while (*part == '/' || *part == '\\') part++;
        if (part[0] == '.' && part[1] == '.' &&
            (!part[2] || part[2] == '/' || part[2] == '\\')) return false;
        part = strpbrk(part, "/\\");
        if (!part) break;
    }
    return true;
}
static bool referenced_file(const char *root, const char *relative) {
    char path[BONGO_CAT_PATH_CAP];
    return safe_reference(relative) &&
        bongo_cat_path_join(path, sizeof(path), root, relative) && bongo_cat_path_is_file(path);
}

static bool referenced_texture(const char *root, const char *relative) {
    char path[BONGO_CAT_PATH_CAP];
    if (!safe_reference(relative) ||
        !bongo_cat_path_join(path, sizeof(path), root, relative)) return false;
    return bongo_cat_image_info(path, NULL, NULL);
}

static bool optional_reference(const char *root, yyjson_val *refs,
    const char *name) {
    yyjson_val *value = yyjson_obj_get(refs, name);
    if (!value) return true;
    const char *relative = yyjson_get_str(value);
    return relative && referenced_file(root, relative);
}

static bool behavior_references(const char *root, yyjson_val *refs) {
    yyjson_val *expressions = yyjson_obj_get(refs, "Expressions");
    if (expressions && !yyjson_is_arr(expressions)) return false;
    size_t index, count; yyjson_val *item;
    yyjson_arr_foreach(expressions, index, count, item)
        if (!referenced_file(root, yyjson_get_str(yyjson_obj_get(item, "File"))))
            return false;
    yyjson_val *motions = yyjson_obj_get(refs, "Motions");
    if (motions && !yyjson_is_obj(motions)) return false;
    size_t group_index, group_count; yyjson_val *key, *group;
    yyjson_obj_foreach(motions, group_index, group_count, key, group) {
        if (!yyjson_is_arr(group)) return false;
        yyjson_arr_foreach(group, index, count, item) {
            if (!referenced_file(root, yyjson_get_str(yyjson_obj_get(item, "File"))))
                return false;
            const char *sound = yyjson_get_str(yyjson_obj_get(item, "Sound"));
            if (sound && !safe_reference(sound)) return false;
        }
    }
    return true;
}
bool bongo_cat_import_manifest_valid(const char *root, const char *setting,
    BongoCatError *error) {
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), root, setting)) return false;
    FILE *file = bongo_cat_file_open(path, "rb");
    yyjson_doc *document = file ? yyjson_read_fp(file, 0, NULL, NULL) : NULL;
    if (file) fclose(file);
    yyjson_val *manifest = document ? yyjson_doc_get_root(document) : NULL;
    yyjson_val *refs = yyjson_is_obj(manifest)
        ? yyjson_obj_get(manifest, "FileReferences") : NULL;
    const char *moc = yyjson_get_str(yyjson_obj_get(refs, "Moc"));
    yyjson_val *textures = yyjson_obj_get(refs, "Textures");
    bool valid = yyjson_get_int(yyjson_obj_get(manifest, "Version")) == 3 &&
        yyjson_is_obj(refs) && referenced_file(root, moc) && yyjson_is_arr(textures) &&
        yyjson_arr_size(textures) > 0;
    size_t index, maximum; yyjson_val *texture;
    yyjson_arr_foreach(textures, index, maximum, texture)
        valid = valid && referenced_texture(root, yyjson_get_str(texture));
    valid = valid && optional_reference(root, refs, "Physics") &&
        optional_reference(root, refs, "Pose") &&
        optional_reference(root, refs, "DisplayInfo") && behavior_references(root, refs);
    yyjson_doc_free(document);
    if (!valid && error) bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
        "Model manifest or referenced assets are invalid: %s", path);
    return valid;
}

static bool path_parent(const char *path, char *parent, size_t capacity) {
    size_t length = path ? strlen(path) : 0;
    while (length && (path[length - 1] == '/' || path[length - 1] == '\\')) length--;
    while (length && path[length - 1] != '/' && path[length - 1] != '\\') length--;
    while (length > 1 && (path[length - 1] == '/' || path[length - 1] == '\\')) length--;
    if (!length || length >= capacity) return false;
    memcpy(parent, path, length);
    parent[length] = '\0';
    return true;
}

static bool ascii_contains_ci(const char *value, const char *needle) {
    if (!value || !needle || !needle[0]) return false;
    size_t length = strlen(needle);
    for (const char *cursor = value; *cursor; ++cursor)
        if (SDL_strncasecmp(cursor, needle, length) == 0) return true;
    return false;
}

static bool utf8_contains(const char *value, const unsigned char *needle,
    size_t length) {
    if (!value || !needle || !length) return false;
    for (const unsigned char *cursor = (const unsigned char *)value; *cursor;
        ++cursor) {
        size_t remaining = strlen((const char *)cursor);
        if (remaining >= length && memcmp(cursor, needle, length) == 0)
            return true;
    }
    return false;
}

static BongoCatModelMode import_mode(const char *path) {
    const char *cursor = path;
    BongoCatModelMode mode = BONGO_CAT_MODE_STANDARD;
    while (cursor && *cursor) {
        while (*cursor == '/' || *cursor == '\\') cursor++;
        const char *end = strpbrk(cursor, "/\\");
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        if (length == 8 && SDL_strncasecmp(cursor, "keyboard", length) == 0)
            mode = BONGO_CAT_MODE_KEYBOARD;
        else if (length == 7 && SDL_strncasecmp(cursor, "gamepad", length) == 0)
            mode = BONGO_CAT_MODE_GAMEPAD;
        else if (length == 8 && SDL_strncasecmp(cursor, "standard", length) == 0)
            mode = BONGO_CAT_MODE_STANDARD;
        if (!end) break;
        cursor = end + 1;
    }
    const char *name = bongo_cat_path_name(path);
    static const unsigned char keyboard_cn[] = {0xe9,0x94,0xae,0xe7,0x9b,0x98};
    static const unsigned char gamepad_cn[] = {0xe6,0x89,0x8b,0xe6,0x9f,0x84};
    static const unsigned char standard_cn[] = {0xe6,0xa0,0x87,0xe5,0x87,0x86};
    if (name && (ascii_contains_ci(name, "keyboard") ||
            utf8_contains(name, keyboard_cn, sizeof(keyboard_cn))))
        return BONGO_CAT_MODE_KEYBOARD;
    if (name && (ascii_contains_ci(name, "gamepad") ||
            utf8_contains(name, gamepad_cn, sizeof(gamepad_cn))))
        return BONGO_CAT_MODE_GAMEPAD;
    if (name && (ascii_contains_ci(name, "standard") ||
            utf8_contains(name, standard_cn, sizeof(standard_cn))))
        return BONGO_CAT_MODE_STANDARD;
    return mode;
}

static bool has_preview_assets(const char *directory) {
    const char *names[] = {"resources", "cover.png", "cat.png", "bg.png",
        "mousebg.png", "tabletbg.png"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        char path[BONGO_CAT_PATH_CAP];
        if (!bongo_cat_path_join(path, sizeof(path), directory, names[i])) continue;
        if (bongo_cat_path_is_file(path) || bongo_cat_path_is_dir(path)) return true;
    }
    return false;
}

static bool has_cover_asset(const char *directory) {
    const char *names[] = {"resources/cover.png", "cover.png", "cat.png", "bg.png"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        char path[BONGO_CAT_PATH_CAP];
        if (bongo_cat_path_join(path, sizeof(path), directory, names[i]) &&
            bongo_cat_path_is_file(path)) return true;
    }
    return false;
}

static bool has_background_asset(const char *directory) {
    const char *names[] = {"resources/background.png", "background.png",
        "bg.png", "mousebg.png", "tabletbg.png"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        char path[BONGO_CAT_PATH_CAP];
        if (bongo_cat_path_join(path, sizeof(path), directory, names[i]) &&
            bongo_cat_path_is_file(path)) return true;
    }
    return false;
}

typedef struct TauriRightKeys {
    bool gamepad;
} TauriRightKeys;

static bool gamepad_key_name(const char *name) {
    static const char *const names[] = {
        "South", "East", "West", "North", "LeftTrigger", "RightTrigger",
        "LeftTrigger2", "RightTrigger2", "LeftThumb", "RightThumb",
        "DPadLeft", "DPadRight", "DPadUp", "DPadDown", "Start", "Select"
    };
    size_t length = name ? strlen(name) : 0;
    if (length <= 4 || SDL_strcasecmp(name + length - 4, ".png") != 0)
        return false;
    length -= 4;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (strlen(names[i]) == length &&
            SDL_strncasecmp(name, names[i], length) == 0) return true;
    return false;
}

static BongoCatPathVisit inspect_right_key(void *userdata,
    const char *dirname, const char *name) {
    (void)dirname;
    TauriRightKeys *keys = userdata;
    if (gamepad_key_name(name)) keys->gamepad = true;
    return BONGO_CAT_PATH_CONTINUE;
}

static bool tauri_resource_mode(const char *directory, const char *assets,
    BongoCatModelMode *mode, bool *gamepad_buttons) {
    const char *roots[] = {directory, assets};
    bool right_keys = false;
    TauriRightKeys keys = {0};
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i) {
        if (!roots[i] || !roots[i][0] || (i && strcmp(roots[0], roots[i]) == 0))
            continue;
        const char *prefixes[] = {"resources", NULL};
        for (size_t j = 0; j < sizeof(prefixes) / sizeof(prefixes[0]); ++j) {
            char parent[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
            const char *root = roots[i];
            if (prefixes[j]) {
                if (!bongo_cat_path_join(parent, sizeof(parent), root,
                        prefixes[j])) continue;
                root = parent;
            }
            if (!bongo_cat_path_join(path, sizeof(path), root, "right-keys") ||
                !bongo_cat_path_is_dir(path)) continue;
            right_keys = true;
            if (!bongo_cat_path_enumerate(path, inspect_right_key, &keys))
                return false;
        }
    }
    if (!right_keys) return false;
    *mode = keys.gamepad ? BONGO_CAT_MODE_GAMEPAD : BONGO_CAT_MODE_KEYBOARD;
    *gamepad_buttons = keys.gamepad;
    return true;
}

bool bongo_cat_import_tauri_add_candidate(BongoCatImportDiscovery *discovery,
    const char *directory, const char *setting) {
    if (discovery->count >= BONGO_CAT_IMPORT_CANDIDATE_CAP) return false;
    for (size_t i = 0; i < discovery->count; ++i)
        if (strcmp(discovery->candidates[i].directory, directory) == 0) {
            if (strcmp(discovery->candidates[i].setting, setting) == 0) return true;
            discovery->ambiguous = true;
            return false;
        }
    BongoCatImportCandidate *candidate = &discovery->candidates[discovery->count++];
    snprintf(candidate->directory, sizeof(candidate->directory), "%s", directory);
    snprintf(candidate->setting, sizeof(candidate->setting), "%s", setting);
    snprintf(candidate->assets, sizeof(candidate->assets), "%s", directory);
    snprintf(candidate->package_root, sizeof(candidate->package_root), "%s", directory);
    candidate->format = BONGO_CAT_IMPORT_TAURI;
    char parent[BONGO_CAT_PATH_CAP];
    if ((!has_cover_asset(directory) || !has_background_asset(directory)) &&
        path_parent(directory, parent, sizeof(parent)) && has_preview_assets(parent))
        snprintf(candidate->assets, sizeof(candidate->assets), "%s", parent);
    if (!tauri_resource_mode(candidate->directory, candidate->assets,
            &candidate->mode, &candidate->gamepad_buttons)) {
        candidate->mode = import_mode(candidate->directory);
        candidate->gamepad_buttons = candidate->mode == BONGO_CAT_MODE_GAMEPAD;
    }
    return true;
}
