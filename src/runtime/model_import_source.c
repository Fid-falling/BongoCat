#include "model_import.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static bool suffix_matches(const char *value, const char *suffix) {
    size_t value_length = value ? strlen(value) : 0;
    size_t suffix_length = suffix ? strlen(suffix) : 0;
    return value_length >= suffix_length && SDL_strcasecmp(
        value + value_length - suffix_length, suffix) == 0;
}

static bool parent_path(const char *path, char *parent, size_t capacity) {
    size_t length = path ? strlen(path) : 0;
    while (length && (path[length - 1] == '/' || path[length - 1] == '\\'))
        length--;
    while (length && path[length - 1] != '/' && path[length - 1] != '\\')
        length--;
    while (length > 1 && (path[length - 1] == '/' || path[length - 1] == '\\'))
        length--;
    if (!length || length >= capacity) return false;
    memcpy(parent, path, length);
    parent[length] = '\0';
    return true;
}

static bool image_package_root(const char *source, char *directory,
    size_t capacity) {
    char current[BONGO_CAT_PATH_CAP];
    if (!parent_path(source, current, sizeof(current))) return false;
    for (int depth = 0; depth < 12; ++depth) {
        if (SDL_strcasecmp(bongo_cat_path_name(current), "img") == 0)
            return parent_path(current, directory, capacity);
        char parent[BONGO_CAT_PATH_CAP];
        if (!parent_path(current, parent, sizeof(parent))) return false;
        snprintf(current, sizeof(current), "%s", parent);
    }
    return false;
}

BongoCatResult bongo_cat_import_source_directory(const char *source,
    char *directory, size_t capacity, BongoCatError *error) {
    if (!source || !source[0] || !directory || !capacity) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
            "Missing model import source");
        return BONGO_CAT_ERROR_ARGUMENT;
    }
    if (bongo_cat_path_is_dir(source)) {
        int length = snprintf(directory, capacity, "%s", source);
        if (length >= 0 && (size_t)length < capacity) return BONGO_CAT_OK;
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
            "Model import path is too long");
        return BONGO_CAT_ERROR_ARGUMENT;
    }
    if (!bongo_cat_path_is_file(source)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
            "Selected model source does not exist: %s", source);
        return BONGO_CAT_ERROR_ARGUMENT;
    }
    const char *name = bongo_cat_path_name(source);
    if (name && suffix_matches(name, ".png") &&
        image_package_root(source, directory, capacity)) return BONGO_CAT_OK;
    if (!name || (SDL_strcasecmp(name, "config.json") != 0 &&
        !suffix_matches(name, ".model3.json"))) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Select an Mver config.json, image-patch PNG, or Live2D .model3.json file");
        return BONGO_CAT_ERROR_FORMAT;
    }
    if (parent_path(source, directory, capacity)) return BONGO_CAT_OK;
    bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
        "Cannot determine the selected model file directory: %s", source);
    return BONGO_CAT_ERROR_ARGUMENT;
}
