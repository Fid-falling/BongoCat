#include "model_import.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <string.h>

static bool suffix(const char *name, const char *ending) {
    size_t name_length = name ? strlen(name) : 0;
    size_t ending_length = ending ? strlen(ending) : 0;
    return name_length >= ending_length &&
        strcmp(name + name_length - ending_length, ending) == 0;
}

static BongoCatPathVisit discover_item(void *userdata,
    const char *dirname, const char *name) {
    BongoCatImportDiscovery *discovery = userdata;
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), dirname, name))
        return BONGO_CAT_PATH_FAILURE;
    if (!bongo_cat_path_is_file(path) || !suffix(name, ".model3.json"))
        return BONGO_CAT_PATH_CONTINUE;
    if (bongo_cat_import_manifest_valid(dirname, name, NULL) &&
        !bongo_cat_import_tauri_add_candidate(discovery, dirname, name))
        return BONGO_CAT_PATH_FAILURE;
    return BONGO_CAT_PATH_CONTINUE;
}

int bongo_cat_import_tauri_discover_exact(const char *source,
    BongoCatImportDiscovery *discovery, BongoCatError *error) {
    if (!source || !discovery || !bongo_cat_path_is_dir(source)) return 0;
    if (!discovery->source_name[0]) snprintf(discovery->source_name,
        sizeof(discovery->source_name), "%s", bongo_cat_path_name(source));
    size_t before = discovery->count;
    if (!bongo_cat_path_enumerate(source, discover_item, discovery)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            discovery->ambiguous
                ? "A model directory contains multiple model3 manifests"
                : "Cannot inspect nearby model directory");
        return -1;
    }
    for (size_t i = before; i < discovery->count; ++i)
        snprintf(discovery->candidates[i].package_root,
            sizeof(discovery->candidates[i].package_root), "%s", source);
    return discovery->count > before ? 1 : 0;
}
