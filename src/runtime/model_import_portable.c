#include "model_import.h"
#include "model_storage.h"
#include "runtime.h"
#include "bongo_cat_neo/json.h"
#include "bongo_cat_neo/path.h"
#include "bongo_cat_neo/sha256.h"

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

#define PORTABLE_DIRECTORY "portable-mver"
#define PORTABLE_MARKER ".bongo-cat-neo-portable.json"

typedef struct PortableStamp {
    uint64_t sum, exclusive, bytes, latest, files, entries;
} PortableStamp;

typedef struct StampWalk {
    PortableStamp *stamp;
    int depth;
} StampWalk;

static uint64_t text_hash(const char *value) {
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char *cursor = (const unsigned char *)value; *cursor; ++cursor) {
        hash ^= *cursor;
        hash *= 1099511628211ull;
    }
    return hash;
}

static uint64_t mix(uint64_t value) {
    value ^= value >> 30; value *= 0xbf58476d1ce4e5b9ull;
    value ^= value >> 27; value *= 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}

static bool stamp_path(const char *path, PortableStamp *stamp, int depth);

static BongoCatNeoPathVisit stamp_item(void *userdata,
    const char *dirname, const char *name) {
    StampWalk *walk = userdata;
    char path[BONGO_CAT_NEO_PATH_CAP];
    return ++walk->stamp->entries <= 16384 &&
        bongo_cat_neo_path_join(path, sizeof(path), dirname, name) &&
        stamp_path(path, walk->stamp, walk->depth)
        ? BONGO_CAT_NEO_PATH_CONTINUE : BONGO_CAT_NEO_PATH_FAILURE;
}

static bool stamp_path(const char *path, PortableStamp *stamp, int depth) {
    if (bongo_cat_neo_path_is_dir(path)) {
        if (depth >= 24) return false;
        StampWalk walk = {stamp, depth + 1};
        return bongo_cat_neo_path_enumerate(path, stamp_item, &walk);
    }
    uint64_t size, modified;
    if (!bongo_cat_neo_path_file_info(path, &size, &modified)) return false;
    if (stamp->files >= 8192 || size > 1073741824ull - stamp->bytes)
        return false;
    uint64_t value = text_hash(path) ^ mix(size) ^ mix(modified);
    value = mix(value);
    stamp->sum += value;
    stamp->exclusive ^= value;
    stamp->bytes += size;
    if (modified > stamp->latest) stamp->latest = modified;
    stamp->files++;
    return true;
}

static bool candidate_signature(const BongoCatNeoImportCandidate *candidate,
    char output[65], BongoCatNeoError *error) {
    char config_hash[65], summary[256];
    if (bongo_cat_neo_sha256_file(candidate->config, config_hash, error) !=
        BONGO_CAT_NEO_OK) return false;
    PortableStamp stamp = {0};
    if (!stamp_path(candidate->directory, &stamp, 0) ||
        !stamp_path(candidate->assets, &stamp, 0) ||
        (candidate->overrides[0] && !stamp_path(candidate->overrides, &stamp, 0))) {
        bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_IO,
            "Cannot inspect portable Mver model assets: %s", candidate->package_root);
        return false;
    }
    int length = snprintf(summary, sizeof(summary), "%s|%016llx|%016llx|%llu|%llu|%llu",
        config_hash, (unsigned long long)stamp.sum,
        (unsigned long long)stamp.exclusive, (unsigned long long)stamp.bytes,
        (unsigned long long)stamp.latest, (unsigned long long)stamp.files);
    if (length < 0 || (size_t)length >= sizeof(summary)) return false;
    bongo_cat_neo_sha256_bytes(summary, (size_t)length, output);
    return true;
}

static bool marker_matches(const char *target, const char *source,
    const char *signature) {
    char path[BONGO_CAT_NEO_PATH_CAP];
    if (!bongo_cat_neo_path_join(path, sizeof(path), target, PORTABLE_MARKER)) return false;
    yyjson_doc *document = bongo_cat_neo_json_read_file(path, 0, NULL);
    yyjson_val *root = document ? yyjson_doc_get_root(document) : NULL;
    const char *stored_source = yyjson_get_str(yyjson_obj_get(root, "source"));
    const char *stored_signature = yyjson_get_str(yyjson_obj_get(root, "signature"));
    bool matches = yyjson_is_obj(root) &&
        yyjson_get_int(yyjson_obj_get(root, "schemaVersion")) == 1 &&
        stored_source && stored_signature && strcmp(source, stored_source) == 0 &&
        strcmp(signature, stored_signature) == 0;
    yyjson_doc_free(document);
    return matches;
}

static bool write_marker(const char *target, const char *source,
    const char *signature, BongoCatNeoModelMode mode) {
    yyjson_mut_doc *document = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = document ? yyjson_mut_obj(document) : NULL;
    if (document) yyjson_mut_doc_set_root(document, root);
    char path[BONGO_CAT_NEO_PATH_CAP];
    bool ok = root && yyjson_mut_obj_add_int(document, root, "schemaVersion", 1) &&
        yyjson_mut_obj_add_str(document, root, "kind", "portable-mver") &&
        yyjson_mut_obj_add_strcpy(document, root, "source", source) &&
        yyjson_mut_obj_add_strcpy(document, root, "signature", signature) &&
        yyjson_mut_obj_add_strcpy(document, root, "mode", bongo_cat_neo_mode_name(mode)) &&
        bongo_cat_neo_path_join(path, sizeof(path), target, PORTABLE_MARKER) &&
        bongo_cat_neo_json_write_file(path, document, YYJSON_WRITE_PRETTY, NULL);
    yyjson_mut_doc_free(document);
    return ok;
}

static bool refresh_cache(const BongoCatNeoImportCandidate *candidate,
    const char *cache_root, const char *id, const char *source,
    const char *signature, bool *created, BongoCatNeoError *error) {
    char target[BONGO_CAT_NEO_PATH_CAP], temporary[BONGO_CAT_NEO_PATH_CAP];
    char backup[BONGO_CAT_NEO_PATH_CAP], name[BONGO_CAT_NEO_ID_CAP + 8];
    snprintf(name, sizeof(name), ".%s.tmp", id);
    if (!bongo_cat_neo_path_join(target, sizeof(target), cache_root, id) ||
        !bongo_cat_neo_path_join(temporary, sizeof(temporary), cache_root, name)) return false;
    snprintf(name, sizeof(name), ".%s.old", id);
    if (!bongo_cat_neo_path_join(backup, sizeof(backup), cache_root, name)) return false;
    bongo_cat_neo_model_remove_tree(temporary, NULL);
    if (!bongo_cat_neo_path_is_dir(target) && bongo_cat_neo_path_is_dir(backup))
        bongo_cat_neo_path_rename(backup, target);
    if (bongo_cat_neo_path_is_dir(target)) bongo_cat_neo_model_remove_tree(backup, NULL);
    *created = !bongo_cat_neo_path_is_dir(target);
    if (!*created && marker_matches(target, source, signature)) return true;
    if (!bongo_cat_neo_path_create_directory(temporary) ||
        !bongo_cat_neo_import_prepare_adapter(candidate, temporary, error) ||
        !write_marker(temporary, source, signature, candidate->mode)) {
        bongo_cat_neo_model_remove_tree(temporary, NULL);
        if (error && !error->message[0]) bongo_cat_neo_error_set(error,
            BONGO_CAT_NEO_ERROR_IO, "Cannot build portable Mver adapter: %s", source);
        return false;
    }
    bool had_target = bongo_cat_neo_path_is_dir(target);
    if (had_target && !bongo_cat_neo_path_rename(target, backup)) {
        bongo_cat_neo_model_remove_tree(temporary, NULL);
        bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_IO,
            "Cannot update portable Mver adapter: %s", SDL_GetError());
        return false;
    }
    if (!bongo_cat_neo_path_rename(temporary, target)) {
        if (had_target) bongo_cat_neo_path_rename(backup, target);
        bongo_cat_neo_model_remove_tree(temporary, NULL);
        bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_IO,
            "Cannot activate portable Mver adapter: %s", SDL_GetError());
        return false;
    }
    bongo_cat_neo_model_remove_tree(backup, NULL);
    return true;
}

static bool add_candidate(BongoCatNeoApp *app, const char *cache_root,
    const char *source, const BongoCatNeoImportCandidate *candidate,
    char *first_created, BongoCatNeoError *error) {
    char source_hash[65], signature[65], id[BONGO_CAT_NEO_ID_CAP];
    bongo_cat_neo_sha256_bytes(source, strlen(source), source_hash);
    snprintf(id, sizeof(id), "mver-%.16s-%s", source_hash,
        bongo_cat_neo_mode_name(candidate->mode));
    if (bongo_cat_neo_models_find(&app->models, id)) return true;
    if (!candidate_signature(candidate, signature, error)) return false;
    bool created = false;
    if (!refresh_cache(candidate, cache_root, id, source, signature, &created, error))
        return false;
    if (app->models.count >= BONGO_CAT_NEO_MODEL_CAP) {
        bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_FORMAT,
            "Too many models while adding portable Mver package");
        return false;
    }
    char adapter[BONGO_CAT_NEO_PATH_CAP];
    if (!bongo_cat_neo_path_join(adapter, sizeof(adapter), cache_root, id)) return false;
    BongoCatNeoModelEntry *entry = &app->models.entries[app->models.count++];
    snprintf(entry->id, sizeof(entry->id), "%s", id);
    snprintf(entry->directory, sizeof(entry->directory), "%s", candidate->directory);
    snprintf(entry->adapter_directory, sizeof(entry->adapter_directory), "%s", adapter);
    snprintf(entry->storage_directory, sizeof(entry->storage_directory), "%s", adapter);
    snprintf(entry->setting_file, sizeof(entry->setting_file), "%s", candidate->setting);
    entry->mode = candidate->mode;
    entry->managed = true;
    if (created && !first_created[0])
        snprintf(first_created, BONGO_CAT_NEO_ID_CAP, "%s", id);
    return true;
}

static BongoCatNeoResult add_discovery(BongoCatNeoApp *app, const char *cache_root,
    const char *source, BongoCatNeoImportDiscovery *discovery,
    char *first_created, BongoCatNeoError *error) {
    for (size_t i = 0; i < discovery->count; ++i) {
        discovery->candidates[i].portable_compat = true;
        if (!add_candidate(app, cache_root, source, &discovery->candidates[i],
            first_created, error)) return error && error->code ? error->code :
                BONGO_CAT_NEO_ERROR_IO;
    }
    return BONGO_CAT_NEO_OK;
}

typedef struct PortableAdd {
    BongoCatNeoApp *app; const char *cache_root; char *first_created;
} PortableAdd;

static BongoCatNeoResult add_scanned(void *userdata, const char *source,
    BongoCatNeoImportDiscovery *discovery, BongoCatNeoError *error) {
    PortableAdd *add = userdata;
    return add_discovery(add->app, add->cache_root, source, discovery,
        add->first_created, error);
}

BongoCatNeoResult bongo_cat_neo_import_portable_mver(BongoCatNeoApp *app,
    const char *root, BongoCatNeoError *error) {
    if (!app || !root || !app->data_root[0]) return BONGO_CAT_NEO_ERROR_ARGUMENT;
    if (!bongo_cat_neo_path_is_dir(root)) return BONGO_CAT_NEO_OK;
    char cache_root[BONGO_CAT_NEO_PATH_CAP], first_created[BONGO_CAT_NEO_ID_CAP] = {0};
    if (!bongo_cat_neo_path_join(cache_root, sizeof(cache_root), app->data_root,
        PORTABLE_DIRECTORY) || !bongo_cat_neo_path_create_directory(cache_root)) return BONGO_CAT_NEO_ERROR_IO;
    BongoCatNeoImportDiscovery *discovery = calloc(1, sizeof(*discovery));
    if (!discovery) return BONGO_CAT_NEO_ERROR_MEMORY;
    int direct = bongo_cat_neo_import_mver_discover_exact(root, discovery, error);
    BongoCatNeoResult result = direct > 0
        ? add_discovery(app, cache_root, root, discovery, first_created, error) :
        BONGO_CAT_NEO_OK;
    if (direct <= 0) {
        if (error) *error = (BongoCatNeoError){0};
        PortableAdd add = {app, cache_root, first_created};
        result = bongo_cat_neo_import_portable_scan(root, add_scanned, &add, error);
    }
    const char *selected = app->config.current_model;
    if (first_created[0] && (!selected[0] || strcmp(selected, "standard") == 0 ||
        strcmp(selected, "keyboard") == 0 || strcmp(selected, "gamepad") == 0))
        snprintf(app->config.current_model, sizeof(app->config.current_model), "%s", first_created);
    free(discovery);
    return result;
}
