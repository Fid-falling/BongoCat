#include "model_storage.h"
#include "bongo_cat_neo/path.h"

#include <SDL3/SDL.h>
#include <ctype.h>
#include <string.h>

static SDL_EnumerationResult SDLCALL remove_item(void *userdata,
    const char *dirname, const char *name) {
    BongoCatNeoError *error = userdata;
    char path[BONGO_CAT_NEO_PATH_CAP];
    return bongo_cat_neo_path_join(path, sizeof(path), dirname, name) &&
        bongo_cat_neo_model_remove_tree(path, error)
        ? SDL_ENUM_CONTINUE : SDL_ENUM_FAILURE;
}

bool bongo_cat_neo_model_remove_tree(const char *path, BongoCatNeoError *error) {
    SDL_PathInfo info;
    if (!path || !path[0] || !SDL_GetPathInfo(path, &info)) return true;
    if (info.type == SDL_PATHTYPE_DIRECTORY &&
        !SDL_EnumerateDirectory(path, remove_item, error)) return false;
    if (SDL_RemovePath(path)) return true;
    if (error) bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_IO,
        "Cannot remove %s: %s", path, SDL_GetError());
    return false;
}

static bool temporary_name(const char *name) {
    static const char prefix[] = ".import-", suffix[] = ".tmp";
    if (!name || strncmp(name, prefix, sizeof(prefix) - 1) != 0) return false;
    const char *cursor = name + sizeof(prefix) - 1;
    const char *dash = strchr(cursor, '-');
    if (!dash || dash == cursor) return false;
    for (const char *value = cursor; value < dash; ++value)
        if (!isxdigit((unsigned char)*value)) return false;
    cursor = dash + 1;
    const char *end = name + strlen(name) - (sizeof(suffix) - 1);
    if (end <= cursor || strcmp(end, suffix) != 0) return false;
    for (const char *value = cursor; value < end; ++value)
        if (!isdigit((unsigned char)*value)) return false;
    return true;
}

static SDL_EnumerationResult SDLCALL cleanup_item(void *userdata,
    const char *dirname, const char *name) {
    BongoCatNeoError *error = userdata;
    if (!temporary_name(name)) return SDL_ENUM_CONTINUE;
    char path[BONGO_CAT_NEO_PATH_CAP];
    SDL_PathInfo info;
    if (!bongo_cat_neo_path_join(path, sizeof(path), dirname, name) ||
        !SDL_GetPathInfo(path, &info)) return SDL_ENUM_CONTINUE;
    if (info.type != SDL_PATHTYPE_DIRECTORY) return SDL_ENUM_CONTINUE;
    return bongo_cat_neo_model_remove_tree(path, error)
        ? SDL_ENUM_CONTINUE : SDL_ENUM_FAILURE;
}

bool bongo_cat_neo_model_cleanup_imports(const char *root, BongoCatNeoError *error) {
    return root && bongo_cat_neo_path_is_dir(root) &&
        SDL_EnumerateDirectory(root, cleanup_item, error);
}
