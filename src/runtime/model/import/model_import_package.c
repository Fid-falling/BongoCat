#include "model_import.h"
#include "runtime.h"
#include "bongo_cat/json.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <yyjson.h>

static bool separator(char value) { return value == '/' || value == '\\'; }

static bool path_prefix(const char *root, const char *path, size_t length) {
#ifdef _WIN32
    return SDL_strncasecmp(root, path, length) == 0;
#else
    return strncmp(root, path, length) == 0;
#endif
}

static bool relative_path(const char *root, const char *path,
    char *relative, size_t capacity) {
    if (!root || !path || !relative || !capacity) return false;
    size_t root_length = strlen(root);
    while (root_length && separator(root[root_length - 1])) root_length--;
    if (!root_length || !path_prefix(root, path, root_length) ||
        (path[root_length] && !separator(path[root_length]))) return false;
    const char *cursor = path + root_length;
    while (separator(*cursor)) cursor++;
    size_t length = strlen(cursor);
    while (length && separator(cursor[length - 1])) length--;
    if (length >= capacity) return false;
    memcpy(relative, cursor, length);
    relative[length] = '\0';
    return true;
}

static bool rebase(const char *source_root, const char *installed_root,
    const char *source, char *target, size_t capacity) {
    if (!source[0]) { target[0] = '\0'; return true; }
    char relative[BONGO_CAT_PATH_CAP];
    if (!relative_path(source_root, source, relative, sizeof(relative))) return false;
    if (!relative[0]) {
        snprintf(target, capacity, "%s", installed_root);
        return true;
    }
    return bongo_cat_path_join(target, capacity, installed_root, relative);
}

static bool copy_relative_directory(const char *source_root,
    const char *source, const char *target_root, BongoCatError *error) {
    char relative[BONGO_CAT_PATH_CAP], target[BONGO_CAT_PATH_CAP];
    if (!relative_path(source_root, source, relative, sizeof(relative)) ||
        !relative[0] || !bongo_cat_path_join(target, sizeof(target),
            target_root, relative)) return false;
    return bongo_cat_copy_directory(source, target, error) == BONGO_CAT_OK;
}

static bool copy_relative_file(const char *source_root, const char *source,
    const char *target_root, BongoCatError *error) {
    char relative[BONGO_CAT_PATH_CAP], target[BONGO_CAT_PATH_CAP];
    if (!relative_path(source_root, source, relative, sizeof(relative)) ||
        !relative[0] || !bongo_cat_path_join(target, sizeof(target),
            target_root, relative)) return false;
    if (bongo_cat_path_is_file(target)) return true;
    size_t length = strlen(target);
    while (length && target[length - 1] != '/' && target[length - 1] != '\\')
        length--;
    if (!length) return false;
    char parent[BONGO_CAT_PATH_CAP];
    memcpy(parent, target, length - 1);
    parent[length - 1] = '\0';
    if (!bongo_cat_path_create_directory(parent) ||
        !bongo_cat_path_copy_file(source, target)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
            "Cannot preserve imported model file: %s", source);
        return false;
    }
    return true;
}

static bool copy_mver_payload(const BongoCatImportCandidate *candidate,
    const char *target, BongoCatError *error) {
    if (!bongo_cat_path_create_directory(target) ||
        !copy_relative_directory(candidate->package_root, candidate->assets,
            target, error)) return false;
    char inside[BONGO_CAT_PATH_CAP];
    if (!relative_path(candidate->assets, candidate->directory,
        inside, sizeof(inside)) &&
        !copy_relative_directory(candidate->package_root, candidate->directory,
            target, error)) return false;
    return copy_relative_file(candidate->package_root, candidate->config,
        target, error);
}

static bool copy_tauri_payload(const BongoCatImportCandidate *candidate,
    const char *target, BongoCatError *error) {
    char relative[BONGO_CAT_PATH_CAP];
    if (!relative_path(candidate->package_root, candidate->directory,
        relative, sizeof(relative))) return false;
    if (!relative[0])
        return bongo_cat_copy_directory(candidate->directory, target, error) ==
            BONGO_CAT_OK;
    return bongo_cat_path_create_directory(target) &&
        copy_relative_directory(candidate->package_root, candidate->directory,
            target, error);
}

static bool copy_payload(const BongoCatImportCandidate *candidate,
    const char *target, BongoCatImportCandidate *installed,
    BongoCatError *error) {
    char payload[BONGO_CAT_PATH_CAP], base[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(payload, sizeof(payload), target, "payload")) return false;
    bool patch = candidate->format == BONGO_CAT_IMPORT_MVER_PATCH;
    if (patch) {
        char patch_path[BONGO_CAT_PATH_CAP];
        if (!bongo_cat_path_create_directory(payload) ||
            !bongo_cat_path_join(base, sizeof(base), payload, "base") ||
            !bongo_cat_path_join(patch_path, sizeof(patch_path), payload, "patch") ||
            !copy_mver_payload(candidate, base, error) ||
            !bongo_cat_path_create_directory(patch_path) ||
            (candidate->overrides[0] && !copy_relative_directory(
                candidate->patch_root, candidate->overrides, patch_path, error)) ||
            !rebase(candidate->patch_root, patch_path, candidate->overrides,
                installed->overrides, sizeof(installed->overrides))) return false;
        snprintf(installed->patch_root, sizeof(installed->patch_root), "%s", patch_path);
    } else if (candidate->format == BONGO_CAT_IMPORT_MVER) {
        snprintf(base, sizeof(base), "%s", payload);
        if (!copy_mver_payload(candidate, base, error)) return false;
    } else {
        snprintf(base, sizeof(base), "%s", payload);
        if (!copy_tauri_payload(candidate, base, error)) return false;
    }
    if (!rebase(candidate->package_root, base, candidate->directory,
            installed->directory, sizeof(installed->directory)) ||
        !rebase(candidate->package_root, base, candidate->assets,
            installed->assets, sizeof(installed->assets)) ||
        !rebase(candidate->package_root, base, candidate->config,
            installed->config, sizeof(installed->config))) return false;
    snprintf(installed->package_root, sizeof(installed->package_root), "%s", base);
    return true;
}

bool bongo_cat_import_prepare_package(const BongoCatImportCandidate *candidate,
    const char *target, BongoCatImportCandidate *installed, BongoCatError *error) {
    if (!candidate || !target || !installed || !bongo_cat_path_create_directory(target)) return false;
    *installed = *candidate;
    if (!copy_payload(candidate, target, installed, error)) {
        if (error && !error->message[0]) bongo_cat_error_set(error,
            BONGO_CAT_ERROR_IO, "Cannot preserve imported model payload");
        return false;
    }
    char adapter[BONGO_CAT_PATH_CAP];
    return bongo_cat_path_join(adapter, sizeof(adapter), target, "adapter") &&
        bongo_cat_path_create_directory(adapter);
}

static const char *format_name(BongoCatImportFormat format) {
    if (format == BONGO_CAT_IMPORT_MVER) return "bongo-cat-mver";
    if (format == BONGO_CAT_IMPORT_MVER_PATCH) return "bongo-cat-mver-patch";
    return "tauri-live2d";
}

static const char *layout_name(BongoCatImportFormat format) {
    if (format == BONGO_CAT_IMPORT_MVER) return "full-package";
    if (format == BONGO_CAT_IMPORT_MVER_PATCH) return "image-patch";
    return "single-model";
}

typedef struct CapabilityName { uint32_t flag; const char *name; } CapabilityName;

static bool add_capabilities(yyjson_mut_doc *document, yyjson_mut_val *root,
    uint32_t capabilities) {
    static const CapabilityName names[] = {
        {BONGO_CAT_MODEL_CAPABILITY_LIVE2D, "live2d"},
        {BONGO_CAT_MODEL_CAPABILITY_PREVIEW, "preview"},
        {BONGO_CAT_MODEL_CAPABILITY_RUNTIME_ADAPTER, "runtime-adapter"},
        {BONGO_CAT_MODEL_CAPABILITY_INPUT_IMAGES, "input-images"},
        {BONGO_CAT_MODEL_CAPABILITY_KEYBOARD_INPUT, "keyboard-input"},
        {BONGO_CAT_MODEL_CAPABILITY_GAMEPAD_INPUT, "gamepad-input"},
        {BONGO_CAT_MODEL_CAPABILITY_BEHAVIORS, "behaviors"},
        {BONGO_CAT_MODEL_CAPABILITY_AUDIO, "audio"},
        {BONGO_CAT_MODEL_CAPABILITY_EFFECTS, "effects"},
        {BONGO_CAT_MODEL_CAPABILITY_MVER_PROJECTION, "mver-projection"},
        {BONGO_CAT_MODEL_CAPABILITY_POINTER_OVERLAY, "pointer-overlay"},
        {BONGO_CAT_MODEL_CAPABILITY_IMAGE_PATCH, "image-patch"}
    };
    yyjson_mut_val *array = yyjson_mut_obj_add_arr(document, root, "capabilities");
    if (!array) return false;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if ((capabilities & names[i].flag) &&
            !yyjson_mut_arr_add_str(document, array, names[i].name)) return false;
    return true;
}

static bool add_extensions(yyjson_mut_doc *document, yyjson_mut_val *root,
    const BongoCatImportCandidate *candidate, const char *target) {
    yyjson_mut_val *extensions = yyjson_mut_obj_add_obj(document, root, "extensions");
    if (!extensions) return false;
    if (candidate->format == BONGO_CAT_IMPORT_TAURI) return true;
    char config[BONGO_CAT_PATH_CAP];
    if (!relative_path(target, candidate->config, config, sizeof(config))) return false;
    yyjson_mut_val *mver = yyjson_mut_obj_add_obj(document, extensions, "mver");
    return mver && yyjson_mut_obj_add_strcpy(document, mver,
        "configuration", config);
}

bool bongo_cat_import_write_package(const BongoCatImportCandidate *candidate,
    const BongoCatPackageMetadata *metadata, const char *target,
    BongoCatError *error) {
    char directory[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
    bool relative = relative_path(target, candidate->directory,
        directory, sizeof(directory));
    if (relative && !directory[0]) snprintf(directory, sizeof(directory), ".");
    yyjson_mut_doc *document = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = document ? yyjson_mut_obj(document) : NULL;
    yyjson_mut_val *source = root ? yyjson_mut_obj_add_obj(document, root, "source") : NULL;
    yyjson_mut_val *model = root ? yyjson_mut_obj_add_obj(document, root, "model") : NULL;
    yyjson_mut_val *runtime = root ? yyjson_mut_obj_add_obj(document, root, "runtime") : NULL;
    if (document) yyjson_mut_doc_set_root(document, root);
    bool ok = candidate && metadata && relative && root && source && model && runtime &&
        yyjson_mut_obj_add_int(document, root, "schemaVersion",
            BONGO_CAT_MODEL_PACKAGE_SCHEMA) &&
        yyjson_mut_obj_add_strcpy(document, root, "packageId", metadata->package_id) &&
        yyjson_mut_obj_add_strcpy(document, root, "contentDigest",
            metadata->content_digest) &&
        (!metadata->family_id[0] || yyjson_mut_obj_add_strcpy(document, root,
            "familyId", metadata->family_id)) &&
        yyjson_mut_obj_add_strcpy(document, root, "displayName",
            metadata->display_name) &&
        yyjson_mut_obj_add_strcpy(document, root, "mode",
            bongo_cat_mode_name(candidate->mode)) &&
        yyjson_mut_obj_add_strcpy(document, source, "format",
            format_name(candidate->format)) &&
        yyjson_mut_obj_add_strcpy(document, source, "name", metadata->source_name) &&
        yyjson_mut_obj_add_strcpy(document, source, "layout",
            layout_name(candidate->format)) &&
        yyjson_mut_obj_add_bool(document, source, "preserved", true) &&
        yyjson_mut_obj_add_strcpy(document, model, "directory", directory) &&
        yyjson_mut_obj_add_strcpy(document, model, "setting", candidate->setting) &&
        yyjson_mut_obj_add_str(document, runtime, "adapter", "adapter") &&
        yyjson_mut_obj_add_str(document, runtime, "metadata",
            "adapter/" BONGO_CAT_MODEL_ADAPTER_FILE) &&
        yyjson_mut_obj_add_int(document, runtime, "adapterSchemaVersion",
            BONGO_CAT_MODEL_ADAPTER_SCHEMA) &&
        yyjson_mut_obj_add_int(document, runtime, "generatorVersion",
            BONGO_CAT_MODEL_ADAPTER_GENERATOR) &&
        add_capabilities(document, root, metadata->capabilities) &&
        add_extensions(document, root, candidate, target) &&
        bongo_cat_path_join(path, sizeof(path), target,
            BONGO_CAT_MODEL_PACKAGE_FILE) &&
        bongo_cat_json_write_file(path, document, YYJSON_WRITE_PRETTY, NULL);
    yyjson_mut_doc_free(document);
    if (!ok) bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
        "Cannot write model package description");
    return ok;
}
