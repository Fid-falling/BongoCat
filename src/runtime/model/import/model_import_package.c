#include "model_import.h"
#include "model_import_mver_copy.h"
#include "tauri/model_import_tauri.h"
#include "bongo_cat/path.h"

#include <stdio.h>

static bool copy_package_files(const BongoCatImportCandidate *candidate,
    const char *target, BongoCatImportCandidate *installed,
    BongoCatError *error) {
    if (candidate->format == BONGO_CAT_IMPORT_TAURI)
        return bongo_cat_import_tauri_convert_to_mver(candidate, target,
            installed, error);
    return bongo_cat_import_mver_copy_package(candidate, target, installed,
        error);
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
