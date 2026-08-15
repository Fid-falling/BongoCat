#include "model_import_portable_internal.h"
#include "bongo_cat/path.h"
#include "bongo_cat/sha256.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct IdentityStamp {
    uint64_t sum, exclusive, bytes, files, entries;
} IdentityStamp;

typedef struct IdentityWalk {
    const char *root;
    const char *group;
    IdentityStamp *stamp;
    int depth;
} IdentityWalk;

static uint64_t hash_text(const char *value) {
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char *cursor = (const unsigned char *)value;
        cursor && *cursor; ++cursor) {
        unsigned char byte = *cursor == '\\' ? '/' : *cursor;
        hash ^= byte; hash *= 1099511628211ull;
    }
    return hash;
}

static uint64_t mix(uint64_t value) {
    value ^= value >> 30; value *= 0xbf58476d1ce4e5b9ull;
    value ^= value >> 27; value *= 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}

static bool stamp_path(const char *path, IdentityWalk *walk);

static BongoCatPathVisit stamp_item(void *userdata,
    const char *dirname, const char *name) {
    IdentityWalk *walk = userdata;
    char path[BONGO_CAT_PATH_CAP];
    return ++walk->stamp->entries <= 16384 &&
        bongo_cat_path_join(path, sizeof(path), dirname, name) &&
        stamp_path(path, walk) ? BONGO_CAT_PATH_CONTINUE :
        BONGO_CAT_PATH_FAILURE;
}

static const char *relative_path(const IdentityWalk *walk, const char *path) {
    size_t root_length = strlen(walk->root);
    if (strncmp(path, walk->root, root_length) != 0) return path;
    const char *relative = path + root_length;
    while (*relative == '/' || *relative == '\\') relative++;
    return relative;
}

static bool stamp_path(const char *path, IdentityWalk *walk) {
    if (bongo_cat_path_is_dir(path)) {
        if (walk->depth >= 24) return false;
        walk->depth++;
        bool ok = bongo_cat_path_enumerate(path, stamp_item, walk);
        walk->depth--;
        return ok;
    }
    uint64_t size = 0;
    if (!bongo_cat_path_file_size(path, &size) || walk->stamp->files >= 8192 ||
        size > 1073741824ull - walk->stamp->bytes) return false;
    char file_hash[65];
    if (bongo_cat_sha256_file(path, file_hash, NULL) != BONGO_CAT_OK)
        return false;
    uint64_t value = mix(hash_text(walk->group) ^
        hash_text(relative_path(walk, path)) ^ hash_text(file_hash) ^ mix(size));
    walk->stamp->sum += value;
    walk->stamp->exclusive ^= value;
    walk->stamp->bytes += size;
    walk->stamp->files++;
    return true;
}

static bool stamp_root(const char *root, const char *group,
    IdentityStamp *stamp) {
    if (!root || !root[0]) return true;
    IdentityWalk walk = {root, group, stamp, 0};
    return stamp_path(root, &walk);
}

bool bongo_cat_import_candidate_digest(const BongoCatImportCandidate *candidate,
    char output[65], BongoCatError *error) {
    if (!candidate || !output) return false;
    char config_hash[65] = {0}, setting[BONGO_CAT_PATH_CAP], setting_hash[65];
    if ((candidate->config[0] && bongo_cat_sha256_file(candidate->config,
        config_hash, error) != BONGO_CAT_OK) ||
        !bongo_cat_path_join(setting, sizeof(setting),
        candidate->directory, candidate->setting) ||
        bongo_cat_sha256_file(setting, setting_hash, error) != BONGO_CAT_OK)
        return false;
    IdentityStamp stamp = {0};
    if (!stamp_root(candidate->directory, "model", &stamp) ||
        !stamp_root(candidate->assets, "assets", &stamp) ||
        !stamp_root(candidate->overrides, "overrides", &stamp)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
            "Cannot fingerprint imported model assets: %s",
            candidate->package_root);
        return false;
    }
    char summary[384];
    int length = snprintf(summary, sizeof(summary),
        "%s|%s|%s|%d|%016llx|%016llx|%llu|%llu", config_hash, setting_hash,
        bongo_cat_mode_name(candidate->mode), (int)candidate->format,
        (unsigned long long)stamp.sum,
        (unsigned long long)stamp.exclusive,
        (unsigned long long)stamp.bytes,
        (unsigned long long)stamp.files);
    if (length < 0 || (size_t)length >= sizeof(summary)) return false;
    bongo_cat_sha256_bytes(summary, (size_t)length, output);
    return true;
}

bool bongo_cat_portable_identity(const BongoCatImportCandidate *candidate,
    char output[65], BongoCatError *error) {
    return bongo_cat_import_candidate_digest(candidate, output, error);
}
