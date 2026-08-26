#include "model_import.h"
#include "model_import_nearby_internal.h"
#include "runtime.h"
#include "bongo_cat/path.h"
#include "bongo_cat/sha256.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct InstalledModelScan {
    BongoCatApp *app;
    const char *cache_root;
    BongoCatError *error;
    BongoCatResult result;
} InstalledModelScan;

static bool existing_identity(const BongoCatModelCatalog *models,
    const char *identity, BongoCatModelMode mode) {
    for (size_t i = 0; models && i < models->count; ++i) {
        const BongoCatModelEntry *entry = &models->entries[i];
        if (entry->mode == mode && entry->content_digest[0] &&
            strcmp(entry->content_digest, identity) == 0) return true;
    }
    return false;
}

static BongoCatModelSourceFormat source_format(
    const BongoCatImportCandidate *candidate) {
    if (candidate->format == BONGO_CAT_IMPORT_MVER_PATCH)
        return BONGO_CAT_MODEL_SOURCE_MVER_PATCH;
    return candidate->format == BONGO_CAT_IMPORT_TAURI
        ? BONGO_CAT_MODEL_SOURCE_TAURI : BONGO_CAT_MODEL_SOURCE_MVER;
}

static const char *variant_name(const BongoCatImportCandidate *candidate) {
    if (candidate->patch_root[0]) {
        const char *name = bongo_cat_path_name(candidate->patch_root);
        if (name && name[0]) return name;
    }
    return bongo_cat_mode_name(candidate->mode);
}

static void describe_entry(BongoCatModelEntry *entry,
    const BongoCatImportCandidate *candidate, const char *id,
    const char *identity, const char *family, const char *storage,
    const char *adapter, const char *display) {
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->id, sizeof(entry->id), "%s", id);
    snprintf(entry->package_id, sizeof(entry->package_id), "%s", id);
    snprintf(entry->content_digest, sizeof(entry->content_digest), "%s",
        identity);
    snprintf(entry->family_id, sizeof(entry->family_id), "%s", family);
    snprintf(entry->display_name, sizeof(entry->display_name), "%s", display);
    snprintf(entry->directory, sizeof(entry->directory), "%s",
        candidate->directory);
    snprintf(entry->adapter_directory, sizeof(entry->adapter_directory), "%s",
        adapter);
    snprintf(entry->storage_directory, sizeof(entry->storage_directory), "%s",
        storage);
    snprintf(entry->setting_file, sizeof(entry->setting_file), "%s",
        candidate->setting);
    entry->mode = candidate->mode;
    entry->source_format = source_format(candidate);
    entry->capabilities = bongo_cat_import_candidate_capabilities(candidate);
    entry->adapter_schema = BONGO_CAT_MODEL_ADAPTER_SCHEMA;
    entry->adapter_generator = BONGO_CAT_MODEL_ADAPTER_GENERATOR;
}

static bool add_package(InstalledModelScan *scan, const char *directory,
    BongoCatImportDiscovery *discovery) {
    const char *folder = bongo_cat_path_name(directory);
    char package_id[BONGO_CAT_ID_CAP], path_hash[65], family[BONGO_CAT_ID_CAP];
    if (!bongo_cat_import_package_id(package_id, sizeof(package_id), folder) ||
        !package_id[0]) return false;
    bongo_cat_sha256_bytes(directory, strlen(directory), path_hash);
    snprintf(family, sizeof(family), "family-installed-%.16s", path_hash);
    size_t emitted = 0;
    for (size_t i = 0; i < discovery->count; ++i) {
        BongoCatImportCandidate *candidate = &discovery->candidates[i];
        char cache_id[BONGO_CAT_ID_CAP], adapter[BONGO_CAT_PATH_CAP];
        char signature[65], identity[65];
        snprintf(cache_id, sizeof(cache_id), "installed-%.16s-%zu",
            path_hash, i + 1);
        if (!bongo_cat_path_join(adapter, sizeof(adapter), scan->cache_root,
                cache_id) ||
            !bongo_cat_nearby_signature(candidate, signature, scan->error))
            return false;
        bool placeholder = false;
        bool cached = bongo_cat_nearby_cached_inspection(adapter, directory,
            signature, identity, &placeholder);
        if (!cached && !bongo_cat_import_candidate_inspect(candidate, identity,
                &placeholder, scan->error)) return false;
        if (placeholder) {
            if (!cached) bongo_cat_nearby_remember_inspection(adapter,
                directory, signature, identity, true, candidate->mode);
            continue;
        }
        char id[BONGO_CAT_ID_CAP];
        if (!bongo_cat_import_variant_id(id, sizeof(id), package_id, emitted)) {
            bongo_cat_error_set(scan->error, BONGO_CAT_ERROR_FORMAT,
                "Installed model id is invalid: %s", package_id);
            return false;
        }
        emitted++;
        if (bongo_cat_settings_model_removed(&scan->app->settings, id)) {
            if (!cached) bongo_cat_nearby_remember_inspection(adapter,
                directory, signature, identity, false, candidate->mode);
            continue;
        }
        if (existing_identity(&scan->app->models, identity, candidate->mode)) {
            if (!cached) bongo_cat_nearby_remember_inspection(adapter,
                directory, signature, identity, false, candidate->mode);
            continue;
        }
        if (scan->app->models.count >= BONGO_CAT_MODEL_CAP) {
            bongo_cat_error_set(scan->error, BONGO_CAT_ERROR_FORMAT,
                "Too many installed model variants");
            return false;
        }
        bool created = false;
        if (!bongo_cat_nearby_refresh_cache(candidate, scan->cache_root,
                cache_id, directory, signature, identity, false, adapter,
                &created, scan->error)) return false;
        (void)created;
        if (bongo_cat_models_find(&scan->app->models, id)) {
            bongo_cat_error_set(scan->error, BONGO_CAT_ERROR_FORMAT,
                "Installed model id is not unique: %s", package_id);
            return false;
        }
        char display[BONGO_CAT_ID_CAP];
        if (discovery->count > 1)
            snprintf(display, sizeof(display), "%s - %s", package_id,
                variant_name(candidate));
        else snprintf(display, sizeof(display), "%s", package_id);
        BongoCatModelEntry *entry =
            &scan->app->models.entries[scan->app->models.count++];
        describe_entry(entry, candidate, id, identity, family, directory,
            adapter, display);
    }
    return true;
}

static BongoCatPathVisit scan_package(void *userdata,
    const char *dirname, const char *name) {
    InstalledModelScan *scan = userdata;
    if (!name || name[0] == '.') return BONGO_CAT_PATH_CONTINUE;
    char directory[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(directory, sizeof(directory), dirname, name)) {
        scan->result = BONGO_CAT_ERROR_FORMAT;
        return BONGO_CAT_PATH_FAILURE;
    }
    if (!bongo_cat_import_authored_package(directory))
        return BONGO_CAT_PATH_CONTINUE;
    BongoCatImportDiscovery *discovery = calloc(1, sizeof(*discovery));
    if (!discovery) {
        scan->result = BONGO_CAT_ERROR_MEMORY;
        return BONGO_CAT_PATH_FAILURE;
    }
    BongoCatError local = {0};
    bool found = bongo_cat_import_discover(directory, discovery, &local);
    if (!found) {
        free(discovery);
        return BONGO_CAT_PATH_CONTINUE;
    }
    bool added = add_package(scan, directory, discovery);
    free(discovery);
    if (added) return BONGO_CAT_PATH_CONTINUE;
    scan->result = scan->error && scan->error->code
        ? scan->error->code : BONGO_CAT_ERROR_IO;
    return BONGO_CAT_PATH_FAILURE;
}

BongoCatResult bongo_cat_import_installed_models(BongoCatApp *app,
    const char *root, BongoCatError *error) {
    if (!app || !root || !app->cache_root[0]) return BONGO_CAT_ERROR_ARGUMENT;
    if (!bongo_cat_path_is_dir(root)) return BONGO_CAT_OK;
    char cache_root[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(cache_root, sizeof(cache_root), app->cache_root,
            BONGO_CAT_ADAPTER_CACHE_DIRECTORY) ||
        !bongo_cat_path_create_directory(cache_root)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
            "Cannot create installed model adapter cache");
        return BONGO_CAT_ERROR_IO;
    }
    InstalledModelScan scan = {app, cache_root, error, BONGO_CAT_OK};
    bongo_cat_import_model_scan_lock();
    bool ok = bongo_cat_path_enumerate(root, scan_package, &scan);
    bongo_cat_import_model_scan_unlock();
    if (!ok && scan.result == BONGO_CAT_OK) scan.result = BONGO_CAT_ERROR_IO;
    return scan.result;
}
