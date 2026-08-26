#include "model_import.h"
#include "model_storage.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ImportInstall {
    char id[BONGO_CAT_ID_CAP];
    char temporary[BONGO_CAT_PATH_CAP];
    char target[BONGO_CAT_PATH_CAP];
    bool committed;
} ImportInstall;

struct BongoCatImportSession {
    char root[BONGO_CAT_PATH_CAP];
    BongoCatImportDigestCache *digests;
};

static void cleanup(ImportInstall *install) {
    if (!install) return;
    bongo_cat_model_remove_tree(install->committed
        ? install->target : install->temporary, NULL);
}

static bool same_package(const BongoCatImportDiscovery *left,
    const BongoCatPackageMetadata *left_metadata,
    const BongoCatImportDiscovery *right,
    const BongoCatPackageMetadata *right_metadata) {
    if (!left || !right || left->count != right->count) return false;
    for (size_t i = 0; i < left->count; ++i)
        if (left->candidates[i].mode != right->candidates[i].mode ||
            strcmp(left_metadata[i].content_digest,
                right_metadata[i].content_digest) != 0) return false;
    return true;
}

typedef struct ExistingPackageSearch {
    const BongoCatImportDiscovery *expected;
    const BongoCatPackageMetadata *metadata;
    BongoCatImportDigestCache *digests;
    char id[BONGO_CAT_ID_CAP];
} ExistingPackageSearch;

static BongoCatPathVisit find_existing_package(void *userdata,
    const char *dirname, const char *name) {
    ExistingPackageSearch *search = userdata;
    if (!name || name[0] == '.') return BONGO_CAT_PATH_CONTINUE;
    char directory[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(directory, sizeof(directory), dirname, name) ||
        !bongo_cat_import_authored_package(directory))
        return BONGO_CAT_PATH_CONTINUE;
    BongoCatImportDiscovery *discovery = calloc(1, sizeof(*discovery));
    BongoCatPackageMetadata *metadata = calloc(BONGO_CAT_IMPORT_CANDIDATE_CAP,
        sizeof(*metadata));
    if (!discovery || !metadata) {
        free(discovery);
        free(metadata);
        return BONGO_CAT_PATH_CONTINUE;
    }
    BongoCatError ignored = {0};
    bool matches = bongo_cat_import_discover(directory, discovery, &ignored) &&
        bongo_cat_import_prepare_package_metadata_cached(discovery, metadata,
            search->digests, &ignored) &&
        same_package(search->expected, search->metadata, discovery, metadata);
    free(discovery);
    free(metadata);
    if (!matches) return BONGO_CAT_PATH_CONTINUE;
    return bongo_cat_import_package_id(search->id, sizeof(search->id), name)
        ? BONGO_CAT_PATH_SUCCESS : BONGO_CAT_PATH_CONTINUE;
}

static bool existing_package(BongoCatImportSession *session,
    const BongoCatImportDiscovery *discovery,
    const BongoCatPackageMetadata *metadata, char id[BONGO_CAT_ID_CAP]) {
    ExistingPackageSearch search = {discovery, metadata, session->digests, {0}};
    if (!bongo_cat_path_enumerate(session->root, find_existing_package,
            &search) || !search.id[0]) return false;
    snprintf(id, BONGO_CAT_ID_CAP, "%s", search.id);
    return true;
}

static bool resolve_install_id(const char *base, const char *root,
    char output[BONGO_CAT_ID_CAP], BongoCatError *error) {
    for (size_t suffix = 1; suffix < 10000; ++suffix) {
        char id[BONGO_CAT_ID_CAP];
        int written = suffix == 1
            ? snprintf(id, sizeof(id), "%s", base)
            : snprintf(id, sizeof(id), "%s-%zu", base, suffix);
        if (written < 0 || (size_t)written >= sizeof(id)) continue;
        char path[BONGO_CAT_PATH_CAP];
        bool exists = bongo_cat_path_join(path, sizeof(path), root, id) &&
            (bongo_cat_path_is_dir(path) || bongo_cat_path_is_file(path));
        if (!exists) {
            snprintf(output, BONGO_CAT_ID_CAP, "%s", id);
            return true;
        }
    }
    bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
        "Cannot allocate a unique model directory for %s", base);
    return false;
}

static void fill_receipt(BongoCatImportReceipt *receipt, const char *id,
    size_t count, bool installed) {
    if (!receipt) return;
    receipt->count = count;
    receipt->installed_count = installed ? count : 0;
    for (size_t i = 0; i < count; ++i) {
        bongo_cat_import_variant_id(receipt->ids[i],
            sizeof(receipt->ids[i]), id, i);
        receipt->installed[i] = installed;
    }
}

static BongoCatResult install_discovery(BongoCatImportSession *session,
    BongoCatImportDiscovery *source_discovery,
    BongoCatPackageMetadata *source_metadata,
    BongoCatImportReceipt *receipt, BongoCatError *error) {
    ImportInstall install = {0};
    char temporary_name[BONGO_CAT_ID_CAP + 24];
    snprintf(temporary_name, sizeof(temporary_name), ".import-%.16s-1.tmp",
        source_metadata[0].content_digest);
    if (!bongo_cat_path_join(install.temporary, sizeof(install.temporary),
            session->root, temporary_name)) return BONGO_CAT_ERROR_IO;
    bongo_cat_model_remove_tree(install.temporary, NULL);
    if (!bongo_cat_import_prepare_storage(source_discovery,
            install.temporary, error)) {
        cleanup(&install);
        return error && error->code == BONGO_CAT_ERROR_FORMAT
            ? BONGO_CAT_ERROR_FORMAT : BONGO_CAT_ERROR_IO;
    }

    BongoCatImportDiscovery *normalized = calloc(1, sizeof(*normalized));
    BongoCatPackageMetadata *metadata = calloc(BONGO_CAT_IMPORT_CANDIDATE_CAP,
        sizeof(*metadata));
    if (!normalized || !metadata) {
        free(normalized);
        free(metadata);
        cleanup(&install);
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Cannot validate normalized model package");
        return BONGO_CAT_ERROR_MEMORY;
    }
    bool valid = bongo_cat_import_discover(install.temporary, normalized,
            error) &&
        bongo_cat_import_prepare_package_metadata_cached(normalized, metadata,
            session->digests, error);
    if (!valid) {
        free(normalized);
        free(metadata);
        cleanup(&install);
        return error && error->code ? error->code : BONGO_CAT_ERROR_FORMAT;
    }

    char existing[BONGO_CAT_ID_CAP];
    if (existing_package(session, normalized, metadata, existing)) {
        fill_receipt(receipt, existing, normalized->count, false);
        free(normalized);
        free(metadata);
        cleanup(&install);
        return BONGO_CAT_OK;
    }
    if (!resolve_install_id(source_metadata[0].package_id, session->root,
            install.id, error) ||
        !bongo_cat_path_join(install.target, sizeof(install.target),
            session->root, install.id)) {
        free(normalized);
        free(metadata);
        cleanup(&install);
        return error && error->code ? error->code : BONGO_CAT_ERROR_FORMAT;
    }
    if (!bongo_cat_path_rename(install.temporary, install.target)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
            "Cannot finish model import: %s", SDL_GetError());
        free(normalized);
        free(metadata);
        cleanup(&install);
        return BONGO_CAT_ERROR_IO;
    }
    install.committed = true;
    fill_receipt(receipt, install.id, normalized->count, true);
    free(normalized);
    free(metadata);
    return BONGO_CAT_OK;
}

BongoCatImportSession *bongo_cat_import_session_create(const char *models_root,
    BongoCatError *error) {
    if (!models_root) return NULL;
    BongoCatImportSession *session = calloc(1, sizeof(*session));
    if (!session) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Cannot allocate model import session");
        return NULL;
    }
    session->digests = bongo_cat_import_digest_cache_create();
    int root_length = snprintf(session->root, sizeof(session->root), "%s",
        models_root);
    if (!session->digests || root_length < 0 ||
        (size_t)root_length >= sizeof(session->root) ||
        !bongo_cat_path_create_directory(session->root) ||
        !bongo_cat_model_cleanup_imports(session->root, error)) {
        bongo_cat_import_session_destroy(session);
        return NULL;
    }
    return session;
}

void bongo_cat_import_session_destroy(BongoCatImportSession *session) {
    if (!session) return;
    bongo_cat_import_digest_cache_destroy(session->digests);
    free(session);
}

BongoCatResult bongo_cat_import_session_install(
    BongoCatImportSession *session, const char *source,
    BongoCatImportReceipt *receipt, BongoCatError *error) {
    if (receipt) memset(receipt, 0, sizeof(*receipt));
    if (!session || !source) return BONGO_CAT_ERROR_ARGUMENT;
    char source_directory[BONGO_CAT_PATH_CAP];
    BongoCatResult source_result = bongo_cat_import_source_directory(source,
        source_directory, sizeof(source_directory), error);
    if (source_result != BONGO_CAT_OK) return source_result;
    BongoCatImportDiscovery *discovery = calloc(1, sizeof(*discovery));
    BongoCatPackageMetadata *metadata = calloc(BONGO_CAT_IMPORT_CANDIDATE_CAP,
        sizeof(*metadata));
    if (!discovery || !metadata) {
        free(discovery);
        free(metadata);
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Cannot allocate model import workspace");
        return BONGO_CAT_ERROR_MEMORY;
    }
    if (!bongo_cat_import_discover(source_directory, discovery, error) ||
        !bongo_cat_import_prepare_package_metadata_cached(discovery, metadata,
            session->digests, error)) {
        BongoCatResult result = error && error->code ? error->code :
            BONGO_CAT_ERROR_FORMAT;
        free(discovery);
        free(metadata);
        return result;
    }
    BongoCatResult result = install_discovery(session, discovery, metadata,
        receipt, error);
    free(discovery);
    free(metadata);
    return result;
}

BongoCatResult bongo_cat_import_install(const char *source,
    const char *models_root, BongoCatImportReceipt *receipt,
    BongoCatError *error) {
    if (receipt) memset(receipt, 0, sizeof(*receipt));
    if (!source || !models_root) return BONGO_CAT_ERROR_ARGUMENT;
    BongoCatImportSession *session = bongo_cat_import_session_create(
        models_root, error);
    if (!session) return error && error->code ? error->code :
        BONGO_CAT_ERROR_IO;
    BongoCatResult result = bongo_cat_import_session_install(session,
        source, receipt, error);
    bongo_cat_import_session_destroy(session);
    return result;
}
