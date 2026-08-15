#include "model_import_nearby_internal.h"
#include "bongo_cat/path.h"
#include "bongo_cat/sha256.h"

#include <stdint.h>
#include <stdio.h>

typedef struct NearbyStamp {
    uint64_t sum, exclusive, bytes, latest, files, entries;
} NearbyStamp;

typedef struct StampWalk {
    NearbyStamp *stamp;
    int depth;
} StampWalk;

static uint64_t text_hash(const char *value) {
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char *cursor = (const unsigned char *)value;
        *cursor; ++cursor) {
        hash ^= *cursor;
        hash *= 1099511628211ull;
    }
    return hash;
}

static uint64_t mix(uint64_t value) {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ull;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}

static bool stamp_path(const char *path, NearbyStamp *stamp, int depth);

static BongoCatPathVisit stamp_item(void *userdata,
    const char *dirname, const char *name) {
    StampWalk *walk = userdata;
    char path[BONGO_CAT_PATH_CAP];
    return ++walk->stamp->entries <= 16384 &&
        bongo_cat_path_join(path, sizeof(path), dirname, name) &&
        stamp_path(path, walk->stamp, walk->depth)
        ? BONGO_CAT_PATH_CONTINUE : BONGO_CAT_PATH_FAILURE;
}

static bool stamp_path(const char *path, NearbyStamp *stamp, int depth) {
    if (bongo_cat_path_is_dir(path)) {
        if (depth >= 24) return false;
        StampWalk walk = {stamp, depth + 1};
        return bongo_cat_path_enumerate(path, stamp_item, &walk);
    }
    uint64_t size, modified;
    if (!bongo_cat_path_file_info(path, &size, &modified)) return false;
    if (stamp->files >= 8192 || size > 1073741824ull - stamp->bytes)
        return false;
    uint64_t value = mix(text_hash(path) ^ mix(size) ^ mix(modified));
    stamp->sum += value;
    stamp->exclusive ^= value;
    stamp->bytes += size;
    if (modified > stamp->latest) stamp->latest = modified;
    stamp->files++;
    return true;
}

bool bongo_cat_nearby_signature(const BongoCatImportCandidate *candidate,
    char output[65], BongoCatError *error) {
    char config_hash[65], summary[256];
    if (bongo_cat_sha256_file(candidate->config, config_hash, error) !=
        BONGO_CAT_OK) return false;
    NearbyStamp stamp = {0};
    if (!stamp_path(candidate->directory, &stamp, 0) ||
        !stamp_path(candidate->assets, &stamp, 0) ||
        (candidate->overrides[0] &&
            !stamp_path(candidate->overrides, &stamp, 0))) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
            "Cannot inspect nearby Mver model assets: %s",
            candidate->package_root);
        return false;
    }
    int length = snprintf(summary, sizeof(summary),
        "%s|%016llx|%016llx|%llu|%llu|%llu", config_hash,
        (unsigned long long)stamp.sum,
        (unsigned long long)stamp.exclusive,
        (unsigned long long)stamp.bytes,
        (unsigned long long)stamp.latest,
        (unsigned long long)stamp.files);
    if (length < 0 || (size_t)length >= sizeof(summary)) return false;
    bongo_cat_sha256_bytes(summary, (size_t)length, output);
    return true;
}
