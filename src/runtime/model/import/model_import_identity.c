#include "model_import.h"
#include "bongo_cat/path.h"
#include "bongo_cat/sha256.h"

#include <stdio.h>
#include <string.h>

static uint32_t candidate_capabilities(const BongoCatImportCandidate *candidate) {
    uint32_t result = BONGO_CAT_MODEL_CAPABILITY_LIVE2D |
        BONGO_CAT_MODEL_CAPABILITY_PREVIEW |
        BONGO_CAT_MODEL_CAPABILITY_RUNTIME_ADAPTER |
        BONGO_CAT_MODEL_CAPABILITY_BEHAVIORS;
    if (candidate->format == BONGO_CAT_IMPORT_TAURI) return result;
    result |= BONGO_CAT_MODEL_CAPABILITY_INPUT_IMAGES |
        BONGO_CAT_MODEL_CAPABILITY_KEYBOARD_INPUT |
        BONGO_CAT_MODEL_CAPABILITY_AUDIO |
        BONGO_CAT_MODEL_CAPABILITY_EFFECTS |
        BONGO_CAT_MODEL_CAPABILITY_MVER_PROJECTION;
    if (candidate->mode == BONGO_CAT_MODE_STANDARD)
        result |= BONGO_CAT_MODEL_CAPABILITY_POINTER_OVERLAY;
    if (candidate->mode == BONGO_CAT_MODE_GAMEPAD && candidate->gamepad_buttons)
        result |= BONGO_CAT_MODEL_CAPABILITY_GAMEPAD_INPUT;
    if (candidate->format == BONGO_CAT_IMPORT_MVER_PATCH)
        result |= BONGO_CAT_MODEL_CAPABILITY_IMAGE_PATCH;
    return result;
}

static const char *source_root(const BongoCatImportCandidate *candidate) {
    return candidate->format == BONGO_CAT_IMPORT_MVER_PATCH &&
        candidate->patch_root[0] ? candidate->patch_root : candidate->package_root;
}

static bool parent_path(const char *path, char *parent, size_t capacity) {
    size_t length = path ? strlen(path) : 0;
    while (length && (path[length - 1] == '/' || path[length - 1] == '\\')) length--;
    while (length && path[length - 1] != '/' && path[length - 1] != '\\') length--;
    while (length && (path[length - 1] == '/' || path[length - 1] == '\\')) length--;
    if (!length || length >= capacity) return false;
    memcpy(parent, path, length); parent[length] = '\0';
    return true;
}

static const char *display_source(const BongoCatImportCandidate *candidate,
    char parent[BONGO_CAT_PATH_CAP]) {
    const char *source = source_root(candidate);
    if (candidate->format == BONGO_CAT_IMPORT_MVER_PATCH &&
        parent_path(source, parent, BONGO_CAT_PATH_CAP) &&
        !strcmp(bongo_cat_path_name(source),
            bongo_cat_path_name(candidate->package_root))) return parent;
    return source;
}

bool bongo_cat_import_prepare_package_metadata(
    BongoCatImportDiscovery *discovery,
    BongoCatPackageMetadata *metadata, BongoCatError *error) {
    char family_material[BONGO_CAT_IMPORT_CANDIDATE_CAP * 66 + 32];
    size_t used = 0, output = 0;
    for (size_t i = 0; i < discovery->count; ++i) {
        BongoCatImportCandidate candidate = discovery->candidates[i];
        BongoCatPackageMetadata *item = &metadata[output];
        bool placeholder = false;
        if (!bongo_cat_import_candidate_inspect(&candidate,
            item->content_digest, &placeholder, error)) return false;
        if (placeholder) continue;
        discovery->candidates[output] = candidate;
        int written = snprintf(family_material + used,
            sizeof(family_material) - used, "%s|", item->content_digest);
        if (written < 0 || (size_t)written >= sizeof(family_material) - used)
            return false;
        used += (size_t)written;
        snprintf(item->package_id, sizeof(item->package_id), "model-%s-%s",
            item->content_digest, bongo_cat_mode_name(candidate.mode));
        char parent[BONGO_CAT_PATH_CAP];
        const char *name = bongo_cat_path_name(display_source(&candidate, parent));
        snprintf(item->source_name, sizeof(item->source_name), "%s",
            name && name[0] ? name : "Imported model");
        snprintf(item->display_name, sizeof(item->display_name), "%s",
            item->source_name);
        item->capabilities = candidate_capabilities(&candidate);
        output++;
    }
    discovery->count = output;
    if (!output) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Selected Mver package contains only bundled placeholder modes");
        return false;
    }
    if (output > 1) {
        char digest[65];
        bongo_cat_sha256_bytes(family_material, used, digest);
        for (size_t i = 0; i < output; ++i)
            snprintf(metadata[i].family_id, sizeof(metadata[i].family_id),
                "family-%s", digest);
    }
    return true;
}

const BongoCatModelEntry *bongo_cat_import_find_existing_package(
    const BongoCatModelCatalog *catalog,
    const BongoCatPackageMetadata *metadata, BongoCatModelMode mode) {
    for (size_t i = 0; i < catalog->count; ++i) {
        const BongoCatModelEntry *entry = &catalog->entries[i];
        if (entry->package_schema == BONGO_CAT_MODEL_PACKAGE_SCHEMA &&
            entry->mode == mode &&
            strcmp(entry->content_digest, metadata->content_digest) == 0)
            return entry;
    }
    return NULL;
}

void bongo_cat_import_describe_nearby_entry(BongoCatModelEntry *entry,
    const BongoCatImportCandidate *candidate, const char *id,
    const char *identity, const char *source_hash, const char *source,
    const char *adapter) {
    snprintf(entry->id, sizeof(entry->id), "%s", id);
    snprintf(entry->package_id, sizeof(entry->package_id), "%s", id);
    snprintf(entry->content_digest, sizeof(entry->content_digest), "%s", identity);
    snprintf(entry->family_id, sizeof(entry->family_id), "family-nearby-%.16s",
        source_hash);
    snprintf(entry->directory, sizeof(entry->directory), "%s", candidate->directory);
    snprintf(entry->adapter_directory, sizeof(entry->adapter_directory), "%s", adapter);
    snprintf(entry->storage_directory, sizeof(entry->storage_directory), "%s", source);
    snprintf(entry->setting_file, sizeof(entry->setting_file), "%s", candidate->setting);
    entry->mode = candidate->mode;
    entry->source_format = candidate->format == BONGO_CAT_IMPORT_TAURI
        ? BONGO_CAT_MODEL_SOURCE_TAURI :
        candidate->format == BONGO_CAT_IMPORT_MVER_PATCH
        ? BONGO_CAT_MODEL_SOURCE_MVER_PATCH : BONGO_CAT_MODEL_SOURCE_MVER;
    entry->capabilities = candidate_capabilities(candidate);
    entry->adapter_schema = BONGO_CAT_MODEL_ADAPTER_SCHEMA;
    entry->adapter_generator = BONGO_CAT_MODEL_ADAPTER_GENERATOR;
    entry->managed = true;
}
