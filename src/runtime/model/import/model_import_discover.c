#include "model_import.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void seed_source_name(const char *source,
    BongoCatImportDiscovery *discovery) {
    char path[BONGO_CAT_PATH_CAP];
    int written = snprintf(path, sizeof(path), "%s", source ? source : "");
    if (written < 0 || (size_t)written >= sizeof(path)) return;
    size_t length = strlen(path);
    while (length && (path[length - 1] == '/' || path[length - 1] == '\\'))
        path[--length] = '\0';
    const char *name = bongo_cat_path_name(path);
    if (name && name[0]) snprintf(discovery->source_name,
        sizeof(discovery->source_name), "%s", name);
}

static bool suffix(const char *name, const char *ending) {
    size_t a = name ? strlen(name) : 0, b = ending ? strlen(ending) : 0;
    return a >= b && strcmp(name + a - b, ending) == 0;
}

static BongoCatPathVisit discover_item(void *userdata,
    const char *dirname, const char *name) {
    BongoCatImportDiscovery *discovery = userdata;
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), dirname, name))
        return BONGO_CAT_PATH_FAILURE;
    if (bongo_cat_path_is_file(path) && suffix(name, ".model3.json")) {
        if (bongo_cat_import_manifest_valid(dirname, name, NULL) &&
            !bongo_cat_import_tauri_add_candidate(discovery, dirname, name))
            return BONGO_CAT_PATH_FAILURE;
        return BONGO_CAT_PATH_CONTINUE;
    }
    if (!bongo_cat_path_is_dir(path) || discovery->depth >= 8 || name[0] == '.')
        return BONGO_CAT_PATH_CONTINUE;
    discovery->depth++;
    bool ok = bongo_cat_path_enumerate(path, discover_item, discovery);
    discovery->depth--;
    return ok ? BONGO_CAT_PATH_CONTINUE : BONGO_CAT_PATH_FAILURE;
}

static int rank(const BongoCatImportCandidate *candidate) {
    return candidate->mode == BONGO_CAT_MODE_STANDARD ? 0 :
        candidate->mode == BONGO_CAT_MODE_KEYBOARD ? 1 : 2;
}

static int compare_candidates(const void *left, const void *right) {
    const BongoCatImportCandidate *a = left, *b = right;
    int difference = rank(a) - rank(b);
    return difference ? difference : strcmp(a->directory, b->directory);
}

typedef struct ContainerDiscovery {
    BongoCatImportDiscovery *output;
} ContainerDiscovery;

static BongoCatResult collect_container(void *userdata, const char *source,
    BongoCatImportDiscovery *found, BongoCatError *error) {
    (void)source;
    ContainerDiscovery *container = userdata;
    for (size_t i = 0; i < found->count; ++i) {
        if (container->output->count >= BONGO_CAT_IMPORT_CANDIDATE_CAP) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
                "Selected folder contains too many model variants");
            return BONGO_CAT_ERROR_FORMAT;
        }
        container->output->candidates[container->output->count++] =
            found->candidates[i];
    }
    return BONGO_CAT_OK;
}

bool bongo_cat_import_discover(const char *source,
    BongoCatImportDiscovery *discovery, BongoCatError *error) {
    memset(discovery, 0, sizeof(*discovery));
    seed_source_name(source, discovery);
    int mver = bongo_cat_import_mver_discover(source, discovery, error);
    if (mver > 0) return true;
    if (mver < 0) return false;
    BongoCatError patch_error = {0};
    int patch = bongo_cat_import_mver_patch_discover(source, discovery,
        &patch_error);
    if (patch > 0) return true;
    memset(discovery, 0, sizeof(*discovery));
    seed_source_name(source, discovery);
    int tauri = bongo_cat_import_tauri_discover_exact(source, discovery, error);
    if (tauri > 0) return true;
    if (tauri < 0) return false;
    memset(discovery, 0, sizeof(*discovery));
    seed_source_name(source, discovery);
    ContainerDiscovery container = {discovery};
    BongoCatResult result = bongo_cat_import_scan(source, collect_container,
        &container, error);
    if (result != BONGO_CAT_OK) return false;
    if (discovery->count) {
        char config[BONGO_CAT_PATH_CAP];
        bool is_container = !bongo_cat_import_mver_config_path(source,
            config, sizeof(config));
        if (is_container) for (size_t i = 0; i < discovery->count; ++i)
            if (discovery->candidates[i].format != BONGO_CAT_IMPORT_TAURI)
                snprintf(discovery->candidates[i].package_root,
                    sizeof(discovery->candidates[i].package_root), "%s", source);
        qsort(discovery->candidates, discovery->count,
            sizeof(discovery->candidates[0]), compare_candidates);
        return true;
    }
    if (patch < 0) {
        if (error) *error = patch_error;
        return false;
    }
    if (error) *error = (BongoCatError){0};
    if (!bongo_cat_path_enumerate(source, discover_item, discovery)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            discovery->ambiguous
                ? "A model directory contains multiple model3 manifests"
                : "Cannot scan model directory or it contains too many models");
        return false;
    }
    if (!discovery->count) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Selected directory contains no valid Live2D model3 JSON");
        return false;
    }
    for (size_t i = 0; i < discovery->count; ++i)
        if (discovery->candidates[i].format == BONGO_CAT_IMPORT_TAURI &&
            !discovery->candidates[i].package_root[0])
            snprintf(discovery->candidates[i].package_root,
                sizeof(discovery->candidates[i].package_root), "%s", source);
    qsort(discovery->candidates, discovery->count,
        sizeof(discovery->candidates[0]), compare_candidates);
    return true;
}
