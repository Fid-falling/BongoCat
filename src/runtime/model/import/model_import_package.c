#include "model_import.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static bool separator(char value) {
    return value == '/' || value == '\\';
}

static bool character_equal(char left, char right) {
    if (separator(left) && separator(right)) return true;
#ifdef _WIN32
    if (left >= 'A' && left <= 'Z') left = (char)(left - 'A' + 'a');
    if (right >= 'A' && right <= 'Z') right = (char)(right - 'A' + 'a');
#endif
    return left == right;
}

static bool relative_path(const char *root, const char *path,
    char *relative, size_t capacity) {
    size_t root_length = root ? strlen(root) : 0;
    while (root_length > 1 && separator(root[root_length - 1])) root_length--;
    size_t path_length = path ? strlen(path) : 0;
    if (!root_length || path_length < root_length) return false;
    for (size_t i = 0; i < root_length; ++i)
        if (!character_equal(root[i], path[i])) return false;
    if (path_length > root_length && !separator(path[root_length])) return false;
    const char *value = path + root_length;
    while (separator(*value)) value++;
    int written = snprintf(relative, capacity, "%s", value);
    return written >= 0 && (size_t)written < capacity;
}

static bool rebase(const char *source_root, const char *installed_root,
    const char *source, char *target, size_t capacity) {
    char relative[BONGO_CAT_PATH_CAP];
    if (!relative_path(source_root, source, relative, sizeof(relative)))
        return false;
    if (!relative[0]) {
        int written = snprintf(target, capacity, "%s", installed_root);
        return written >= 0 && (size_t)written < capacity;
    }
    return bongo_cat_path_join(target, capacity, installed_root, relative);
}

static bool copy_relative_file(const char *source_root, const char *source,
    const char *target_root, BongoCatError *error) {
    char relative[BONGO_CAT_PATH_CAP], target[BONGO_CAT_PATH_CAP];
    if (!relative_path(source_root, source, relative, sizeof(relative)) ||
        !relative[0] || !bongo_cat_path_join(target, sizeof(target),
            target_root, relative)) return false;
    if (bongo_cat_path_is_file(target)) return true;
    if (bongo_cat_path_copy_file(source, target)) return true;
    bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
        "Cannot copy imported model file: %s", source);
    return false;
}

typedef struct MverDirectoryCopy {
    const char *target_root;
    BongoCatError *error;
    unsigned depth;
} MverDirectoryCopy;

static bool distribution_file(const char *name) {
    if (!name || name[0] == '.') return true;
    size_t length = strlen(name);
    if (length >= 4 && SDL_strcasecmp(name + length - 4, "_bak") == 0)
        return true;
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    static const char *const extensions[] = {
        ".exe", ".dll", ".pdf", ".bak", ".lock", ".pdb", ".zip"
    };
    for (size_t i = 0; i < sizeof(extensions) / sizeof(extensions[0]); ++i)
        if (SDL_strcasecmp(dot, extensions[i]) == 0) return true;
    return false;
}

static bool copy_mver_tree(const char *source, const char *target,
    unsigned depth, BongoCatError *error);

static bool mver_runtime_resources(const char *directory, const char *name) {
    if (!name || SDL_strcasecmp(name, "Resources") != 0 ||
        !bongo_cat_path_is_dir(directory)) return false;
    char font[BONGO_CAT_PATH_CAP], logo[BONGO_CAT_PATH_CAP];
    return bongo_cat_path_join(font, sizeof(font), directory, "cat.ttf") &&
        bongo_cat_path_join(logo, sizeof(logo), directory, "l2dlogo.png") &&
        bongo_cat_path_is_file(font) && bongo_cat_path_is_file(logo);
}

static BongoCatPathVisit copy_mver_child(void *userdata,
    const char *dirname, const char *name) {
    MverDirectoryCopy *context = userdata;
    char source[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(source, sizeof(source), dirname, name))
        return BONGO_CAT_PATH_FAILURE;
    if (mver_runtime_resources(source, name)) return BONGO_CAT_PATH_CONTINUE;
    if (distribution_file(name)) return BONGO_CAT_PATH_CONTINUE;
    char target[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(target, sizeof(target), context->target_root,
            name)) return BONGO_CAT_PATH_FAILURE;
    if (!bongo_cat_path_is_dir(source)) {
        if (context->depth == 0) return BONGO_CAT_PATH_CONTINUE;
        return bongo_cat_path_copy_file(source, target)
            ? BONGO_CAT_PATH_CONTINUE : BONGO_CAT_PATH_FAILURE;
    }
    return copy_mver_tree(source, target, context->depth + 1, context->error)
        ? BONGO_CAT_PATH_CONTINUE : BONGO_CAT_PATH_FAILURE;
}

static bool copy_mver_tree(const char *source, const char *target,
    unsigned depth, BongoCatError *error) {
    if (depth > 32 || !bongo_cat_path_create_directory(target)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
            "Cannot create imported model directory: %s", target);
        return false;
    }
    MverDirectoryCopy context = {target, error, depth};
    return bongo_cat_path_enumerate(source, copy_mver_child, &context);
}

static bool copy_mver_relative_directory(const char *source_root,
    const char *source, const char *target_root, BongoCatError *error) {
    char relative[BONGO_CAT_PATH_CAP], target[BONGO_CAT_PATH_CAP];
    if (!relative_path(source_root, source, relative, sizeof(relative)) ||
        !relative[0] || !bongo_cat_path_join(target, sizeof(target),
            target_root, relative)) return false;
    return copy_mver_tree(source, target, 1, error);
}

static bool copy_mver_package(const BongoCatImportCandidate *candidate,
    const char *target, BongoCatError *error) {
    if (!bongo_cat_path_create_directory(target)) return false;
    if (!copy_mver_tree(candidate->package_root, target, 0, error)) return false;
    return copy_relative_file(candidate->package_root, candidate->config,
        target, error);
}

static bool copy_package_files(const BongoCatImportCandidate *candidate,
    const char *target, BongoCatImportCandidate *installed,
    BongoCatError *error) {
    char base[BONGO_CAT_PATH_CAP];
    if (candidate->format == BONGO_CAT_IMPORT_TAURI)
        return bongo_cat_import_tauri_convert_to_mver(candidate, target,
            installed, error);
    bool patch = candidate->format == BONGO_CAT_IMPORT_MVER_PATCH;
    if (patch) {
        char patch_relative[BONGO_CAT_PATH_CAP];
        bool patch_inside_package = relative_path(candidate->package_root,
            candidate->patch_root, patch_relative, sizeof(patch_relative));
        const char *patch_source_root = patch_inside_package
            ? candidate->package_root : candidate->patch_root;
        snprintf(base, sizeof(base), "%s", target);
        if (!copy_mver_package(candidate, base, error) ||
            (!patch_inside_package && candidate->overrides[0] &&
                !copy_mver_relative_directory(patch_source_root,
                    candidate->overrides, target, error)) ||
            (candidate->overrides[0] && !rebase(patch_source_root, target,
                candidate->overrides, installed->overrides,
                sizeof(installed->overrides))) ||
            !rebase(patch_source_root, target, candidate->patch_root,
                installed->patch_root, sizeof(installed->patch_root)))
            return false;
    } else if (candidate->format == BONGO_CAT_IMPORT_MVER) {
        snprintf(base, sizeof(base), "%s", target);
        if (!copy_mver_package(candidate, base, error)) return false;
    } else return false;
    if (!rebase(candidate->package_root, base, candidate->directory,
            installed->directory, sizeof(installed->directory)) ||
        !rebase(candidate->package_root, base, candidate->assets,
            installed->assets, sizeof(installed->assets)) ||
        !rebase(candidate->package_root, base, candidate->config,
            installed->config, sizeof(installed->config))) return false;
    installed->format = candidate->format;
    return true;
}

bool bongo_cat_import_prepare_package(const BongoCatImportCandidate *candidate,
    const char *target, BongoCatImportCandidate *installed,
    BongoCatError *error) {
    if (!candidate || !target || !installed ||
        !bongo_cat_path_create_directory(target)) return false;
    *installed = *candidate;
    if (copy_package_files(candidate, target, installed, error)) return true;
    if (error && !error->message[0]) bongo_cat_error_set(error,
        BONGO_CAT_ERROR_IO, "Cannot preserve imported model files");
    return false;
}

static bool tauri_variant_directory(char *output, size_t capacity,
    const char *target, const BongoCatImportDiscovery *discovery,
    size_t index) {
    const char *mode = bongo_cat_mode_name(discovery->candidates[index].mode);
    size_t occurrence = 1;
    for (size_t i = 0; i < index; ++i)
        if (discovery->candidates[i].mode == discovery->candidates[index].mode)
            occurrence++;
    char name[48];
    int written = occurrence == 1
        ? snprintf(name, sizeof(name), "%s", mode)
        : snprintf(name, sizeof(name), "%s-%zu", mode, occurrence);
    return written >= 0 && (size_t)written < sizeof(name) &&
        bongo_cat_path_join(output, capacity, target, name);
}

bool bongo_cat_import_prepare_storage(
    const BongoCatImportDiscovery *discovery, const char *target,
    BongoCatError *error) {
    if (!discovery || !discovery->count || !target) return false;
    bool tauri = true;
    for (size_t i = 0; i < discovery->count; ++i)
        tauri = tauri && discovery->candidates[i].format ==
            BONGO_CAT_IMPORT_TAURI;
    if (!tauri) {
        BongoCatImportCandidate installed;
        return bongo_cat_import_prepare_package(&discovery->candidates[0],
            target, &installed, error);
    }
    for (size_t i = 0; i < discovery->count; ++i) {
        char package[BONGO_CAT_PATH_CAP];
        if (discovery->count == 1)
            snprintf(package, sizeof(package), "%s", target);
        else if (!tauri_variant_directory(package, sizeof(package), target,
                discovery, i)) return false;
        BongoCatImportCandidate installed;
        if (!bongo_cat_import_prepare_package(&discovery->candidates[i],
                package, &installed, error)) return false;
    }
    return true;
}

bool bongo_cat_import_authored_package(const char *directory) {
    char path[BONGO_CAT_PATH_CAP];
    if (!directory || !bongo_cat_path_is_dir(directory)) return false;
    static const char *const internal[] = {
        ".bongo-cat-package.json", ".bongo-cat-adapter",
        BONGO_CAT_MODEL_BUILTIN_MARKER
    };
    for (size_t i = 0; i < sizeof(internal) / sizeof(internal[0]); ++i)
        if (bongo_cat_path_join(path, sizeof(path), directory, internal[i]) &&
            (bongo_cat_path_is_file(path) || bongo_cat_path_is_dir(path)))
            return false;
    return true;
}
