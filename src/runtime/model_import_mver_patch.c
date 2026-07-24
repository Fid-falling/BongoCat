#include "model_import.h"
#include "runtime.h"
#include "bongo_cat_neo/path.h"

#include <stdio.h>
#include <string.h>

typedef struct PatchSearch {
    char image_root[BONGO_CAT_NEO_PATH_CAP];
    int depth;
    int matches;
} PatchSearch;

static bool child_file(const char *root, const char *first, const char *second) {
    char directory[BONGO_CAT_NEO_PATH_CAP], path[BONGO_CAT_NEO_PATH_CAP];
    return bongo_cat_neo_path_join(directory, sizeof(directory), root, first) &&
        bongo_cat_neo_path_join(path, sizeof(path), directory, second) &&
        bongo_cat_neo_path_is_file(path);
}

static bool patch_shape(const char *root) {
    char mode[BONGO_CAT_NEO_PATH_CAP];
    if (bongo_cat_neo_path_join(mode, sizeof(mode), root, "standard") &&
        child_file(mode, "hand", "0.png")) return true;
    return bongo_cat_neo_path_join(mode, sizeof(mode), root, "gamepad") &&
        child_file(mode, "lefthand", "0.png") &&
        child_file(mode, "righthand", "0.png");
}

static SDL_EnumerationResult SDLCALL find_image_root(void *userdata,
    const char *dirname, const char *name) {
    PatchSearch *search = userdata;
    char path[BONGO_CAT_NEO_PATH_CAP];
    if (!bongo_cat_neo_path_join(path, sizeof(path), dirname, name)) return SDL_ENUM_FAILURE;
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_DIRECTORY ||
        name[0] == '.') return SDL_ENUM_CONTINUE;
    if (strcmp(name, "img") == 0 && patch_shape(path)) {
        search->matches++;
        if (search->matches == 1)
            snprintf(search->image_root, sizeof(search->image_root), "%s", path);
        return SDL_ENUM_CONTINUE;
    }
    if (search->depth >= 3) return SDL_ENUM_CONTINUE;
    search->depth++;
    bool ok = SDL_EnumerateDirectory(path, find_image_root, search);
    search->depth--;
    return ok ? SDL_ENUM_CONTINUE : SDL_ENUM_FAILURE;
}

static bool parent_path(const char *path, char *parent, size_t capacity) {
    size_t length = path ? strlen(path) : 0;
    while (length && (path[length - 1] == '/' || path[length - 1] == '\\')) length--;
    while (length && path[length - 1] != '/' && path[length - 1] != '\\') length--;
    while (length > 1 && (path[length - 1] == '/' || path[length - 1] == '\\')) length--;
    if (!length || length >= capacity) return false;
    memcpy(parent, path, length); parent[length] = '\0';
    return true;
}

static bool full_package(const char *path) {
    char config[BONGO_CAT_NEO_PATH_CAP], image[BONGO_CAT_NEO_PATH_CAP];
    return bongo_cat_neo_path_join(config, sizeof(config), path, "config.json") &&
        bongo_cat_neo_path_join(image, sizeof(image), path, "img") &&
        bongo_cat_neo_path_is_file(config) && bongo_cat_neo_path_is_dir(image) &&
        patch_shape(image);
}

static int find_base(const char *source, const char *model_name,
    char *base, size_t capacity) {
    char current[BONGO_CAT_NEO_PATH_CAP];
    int matches = 0;
    snprintf(current, sizeof(current), "%s", source);
    for (int depth = 0; depth < 6; ++depth) {
        char parent[BONGO_CAT_NEO_PATH_CAP], candidate[BONGO_CAT_NEO_PATH_CAP];
        if (!parent_path(current, parent, sizeof(parent))) break;
        if (bongo_cat_neo_path_join(candidate, sizeof(candidate), parent, model_name) &&
            strcmp(candidate, source) != 0 && full_package(candidate)) {
            if (!matches) snprintf(base, capacity, "%s", candidate);
            matches++;
        }
        snprintf(current, sizeof(current), "%s", parent);
    }
    return matches;
}

int bongo_cat_neo_import_mver_patch_discover(const char *source,
    BongoCatNeoImportDiscovery *discovery, BongoCatNeoError *error) {
    PatchSearch search = {0};
    if (bongo_cat_neo_path_name(source)[0] &&
        strcmp(bongo_cat_neo_path_name(source), "img") == 0 && patch_shape(source)) {
        snprintf(search.image_root, sizeof(search.image_root), "%s", source);
        search.matches = 1;
    } else if (!SDL_EnumerateDirectory(source, find_image_root, &search)) return 0;
    if (!search.matches) return 0;
    if (search.matches != 1) {
        bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_FORMAT,
            "Mver image patch contains multiple model roots: %s", source);
        return -1;
    }
    char model_root[BONGO_CAT_NEO_PATH_CAP], base[BONGO_CAT_NEO_PATH_CAP];
    if (!parent_path(search.image_root, model_root, sizeof(model_root))) return -1;
    int bases = find_base(source, bongo_cat_neo_path_name(model_root), base, sizeof(base));
    if (bases != 1) {
        bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_FORMAT,
            bases ? "Mver image patch matches multiple full model packages: %s" :
            "Mver image patch requires a matching full model package: %s", source);
        return -1;
    }
    int result = bongo_cat_neo_import_mver_discover(base, discovery, error);
    if (result <= 0) return result < 0 ? -1 : 0;
    for (size_t i = 0; i < discovery->count; ++i) {
        BongoCatNeoImportCandidate *candidate = &discovery->candidates[i];
        char mode[BONGO_CAT_NEO_PATH_CAP];
        if (bongo_cat_neo_path_join(mode, sizeof(mode), search.image_root,
            bongo_cat_neo_mode_name(candidate->mode)) && bongo_cat_neo_path_is_dir(mode))
            snprintf(candidate->overrides, sizeof(candidate->overrides), "%s", mode);
        snprintf(candidate->patch_root, sizeof(candidate->patch_root), "%s", source);
        candidate->format = BONGO_CAT_NEO_IMPORT_MVER_PATCH;
    }
    return 1;
}
