#include "model_import_path.h"

#include <SDL3/SDL.h>
#include <string.h>

static bool separator(char value) {
    return value == '/' || value == '\\';
}

bool bongo_cat_import_parent_path(const char *path, char *parent,
    size_t capacity) {
    size_t length = path ? strlen(path) : 0;
    while (length && separator(path[length - 1])) length--;
    while (length && !separator(path[length - 1])) length--;
    while (length > 1 && separator(path[length - 1])) length--;
    if (!length || length >= capacity) return false;
    memcpy(parent, path, length);
    parent[length] = '\0';
    return true;
}

bool bongo_cat_import_has_suffix(const char *value, const char *suffix) {
    size_t value_length = value ? strlen(value) : 0;
    size_t suffix_length = suffix ? strlen(suffix) : 0;
    return value_length >= suffix_length && suffix_length > 0 &&
        memcmp(value + value_length - suffix_length, suffix, suffix_length) == 0;
}

bool bongo_cat_import_has_suffix_ci(const char *value, const char *suffix) {
    size_t value_length = value ? strlen(value) : 0;
    size_t suffix_length = suffix ? strlen(suffix) : 0;
    return value_length >= suffix_length && suffix_length > 0 &&
        SDL_strcasecmp(value + value_length - suffix_length, suffix) == 0;
}
