#include "model_import.h"
#include "model_import_lock.h"
#include "model_import_session_internal.h"
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

static void cleanup(ImportInstall *install) {
    if (!install) return;
    bongo_cat_model_remove_tree(install->committed
        ? install->target : install->temporary, NULL);
}

static bool existing_package(BongoCatImportSession *session,
    const BongoCatImportDiscovery *discovery,
    const BongoCatPackageMetadata *metadata,
    char ids[BONGO_CAT_IMPORT_CANDIDATE_CAP][BONGO_CAT_ID_CAP],
    size_t *count) {
    return bongo_cat_import_package_index_find(session->packages,
        discovery, metadata, ids, count);
}

static bool canonical_mver_package(
    const BongoCatImportDiscovery *discovery) {
    if (!discovery || !discovery->count) return false;
    for (size_t i = 0; i < discovery->count; ++i)
        if (discovery->candidates[i].format != BONGO_CAT_IMPORT_MVER)
            return false;
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

static void fill_receipt_ids(BongoCatImportReceipt *receipt,
    const char ids[BONGO_CAT_IMPORT_CANDIDATE_CAP][BONGO_CAT_ID_CAP],
    size_t count, bool installed) {
    if (!receipt) return;
    receipt->count = count;
    receipt->installed_count = installed ? count : 0;
    for (size_t i = 0; i < count; ++i) {
        snprintf(receipt->ids[i], sizeof(receipt->ids[i]), "%s", ids[i]);
        receipt->installed[i] = installed;
    }
}

static BongoCatResult install_discovery(BongoCatImportSession *session,
    BongoCatImportDiscovery *source_discovery,
    BongoCatPackageMetadata *source_metadata,
    BongoCatImportReceipt *receipt, BongoCatError *error) {
    uint64_t started = SDL_GetTicksNS();
    if (canonical_mver_package(source_discovery)) {
        char existing[BONGO_CAT_IMPORT_CANDIDATE_CAP][BONGO_CAT_ID_CAP];
        size_t existing_count = 0;
        if (existing_package(session, source_discovery, source_metadata,
                existing, &existing_count)) {
            SDL_Log("[runtime] Model import storage transaction: "
                "stage=source-duplicate variants=%llu elapsed_ms=%.1f",
                (unsigned long long)existing_count,
                (SDL_GetTicksNS() - started) / 1000000.0);
            fill_receipt_ids(receipt, existing, existing_count, false);
            return BONGO_CAT_OK;
        }
    }

    ImportInstall install = {0};
    char temporary_name[BONGO_CAT_ID_CAP + 24];
    snprintf(temporary_name, sizeof(temporary_name), ".import-%.16s-1.tmp",
        source_metadata[0].content_digest);
    if (!bongo_cat_path_join(install.temporary, sizeof(install.temporary),
            session->root, temporary_name)) return BONGO_CAT_ERROR_IO;
    SDL_Log("[runtime] Model import storage transaction: stage=prepare "
        "candidates=%llu temporary=%s",
        (unsigned long long)source_discovery->count, install.temporary);
    bongo_cat_model_remove_tree(install.temporary, NULL);
    if (!bongo_cat_import_prepare_storage(source_discovery,
            install.temporary, error)) {
        cleanup(&install);
        return error && error->code == BONGO_CAT_ERROR_FORMAT
            ? BONGO_CAT_ERROR_FORMAT : BONGO_CAT_ERROR_IO;
    }
    SDL_Log("[runtime] Model import storage transaction: stage=prepared "
        "elapsed_ms=%.1f temporary=%s",
        (SDL_GetTicksNS() - started) / 1000000.0, install.temporary);

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
    SDL_Log("[runtime] Model import storage transaction: "
        "stage=validate-normalized temporary=%s", install.temporary);
    bool discovered = bongo_cat_import_discover(install.temporary, normalized,
        error);
    SDL_Log("[runtime] Model import storage transaction: "
        "stage=normalized-discovered result=%d candidates=%llu temporary=%s",
        discovered, (unsigned long long)normalized->count, install.temporary);
    bool valid = discovered && bongo_cat_import_prepare_package_metadata_cached(
        normalized, metadata, session->digests, error);
    if (!valid) {
        free(normalized);
        free(metadata);
        cleanup(&install);
        return error && error->code ? error->code : BONGO_CAT_ERROR_FORMAT;
    }

    char existing[BONGO_CAT_IMPORT_CANDIDATE_CAP][BONGO_CAT_ID_CAP];
    size_t existing_count = 0;
    if (existing_package(session, normalized, metadata, existing,
            &existing_count)) {
        SDL_Log("[runtime] Model import storage transaction: stage=duplicate "
            "variants=%llu elapsed_ms=%.1f temporary=%s",
            (unsigned long long)existing_count,
            (SDL_GetTicksNS() - started) / 1000000.0, install.temporary);
        fill_receipt_ids(receipt, existing, existing_count, false);
        free(normalized);
        free(metadata);
        cleanup(&install);
        return BONGO_CAT_OK;
    }
    if (!bongo_cat_import_package_index_has_capacity(session->packages,
            normalized->count)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Too many installed model variants");
        free(normalized);
        free(metadata);
        cleanup(&install);
        return BONGO_CAT_ERROR_FORMAT;
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
    SDL_Log("[runtime] Model import storage transaction: stage=committed "
        "id=%s elapsed_ms=%.1f target=%s", install.id,
        (SDL_GetTicksNS() - started) / 1000000.0, install.target);
    if (!bongo_cat_import_package_index_add(session->packages, install.id,
            normalized, metadata, error)) {
        free(normalized);
        free(metadata);
        cleanup(&install);
        return error && error->code ? error->code : BONGO_CAT_ERROR_FORMAT;
    }
    fill_receipt(receipt, install.id, normalized->count, true);
    free(normalized);
    free(metadata);
    return BONGO_CAT_OK;
}

static BongoCatImportSession *session_create_unlocked(const char *models_root,
    BongoCatError *error) {
    uint64_t started = SDL_GetTicksNS();
    SDL_Log("[runtime] Model import session creation started: root=%s",
        models_root);
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
        !bongo_cat_path_create_directory(session->root)) {
        bongo_cat_import_session_destroy(session);
        return NULL;
    }
    SDL_Log("[runtime] Model import session creation: stage=cleanup root=%s",
        session->root);
    if (!bongo_cat_model_cleanup_imports(session->root, error)) {
        bongo_cat_import_session_destroy(session);
        return NULL;
    }
    SDL_Log("[runtime] Model import session creation: stage=index root=%s",
        session->root);
    session->packages = bongo_cat_import_package_index_create(session->root,
        session->digests, error);
    if (!session->packages) {
        bongo_cat_import_session_destroy(session);
        return NULL;
    }
    SDL_Log("[runtime] Model import session creation completed: "
        "elapsed_ms=%.1f root=%s",
        (SDL_GetTicksNS() - started) / 1000000.0, session->root);
    return session;
}

BongoCatImportSession *bongo_cat_import_session_create(const char *models_root,
    BongoCatError *error) {
    if (!models_root) return NULL;
    SDL_Log("[runtime] Model import session lock: stage=waiting");
    bongo_cat_import_storage_lock();
    SDL_Log("[runtime] Model import session lock: stage=acquired");
    BongoCatImportSession *session = session_create_unlocked(models_root,
        error);
    bongo_cat_import_storage_unlock();
    SDL_Log("[runtime] Model import session lock: stage=released result=%d",
        session != NULL);
    return session;
}

void bongo_cat_import_session_destroy(BongoCatImportSession *session) {
    if (!session) return;
    bongo_cat_import_package_index_destroy(session->packages);
    bongo_cat_import_digest_cache_destroy(session->digests);
    free(session);
}

static BongoCatResult session_install_unlocked(
    BongoCatImportSession *session, const char *source,
    BongoCatImportReceipt *receipt, BongoCatError *error) {
    char source_directory[BONGO_CAT_PATH_CAP];
    uint64_t started = SDL_GetTicksNS();
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
    SDL_Log("[runtime] Model import package discovery started: path=%s",
        source_directory);
    bool discovered = bongo_cat_import_discover(source_directory, discovery,
        error);
    SDL_Log("[runtime] Model import package discovery completed: result=%d "
        "candidates=%llu elapsed_ms=%.1f path=%s", discovered,
        (unsigned long long)discovery->count,
        (SDL_GetTicksNS() - started) / 1000000.0, source_directory);
    bool metadata_ready = discovered &&
        bongo_cat_import_prepare_package_metadata_cached(discovery, metadata,
            session->digests, error);
    SDL_Log("[runtime] Model import package fingerprint completed: result=%d "
        "candidates=%llu elapsed_ms=%.1f path=%s", metadata_ready,
        (unsigned long long)discovery->count,
        (SDL_GetTicksNS() - started) / 1000000.0, source_directory);
    if (!metadata_ready) {
        BongoCatResult result = error && error->code ? error->code :
            BONGO_CAT_ERROR_FORMAT;
        free(discovery);
        free(metadata);
        return result;
    }
    BongoCatResult result = install_discovery(session, discovery, metadata,
        receipt, error);
    SDL_Log("[runtime] Model import package completed: result=%d variants=%llu "
        "installed=%llu elapsed_ms=%.1f path=%s", (int)result,
        (unsigned long long)(receipt ? receipt->count : 0),
        (unsigned long long)(receipt ? receipt->installed_count : 0),
        (SDL_GetTicksNS() - started) / 1000000.0, source_directory);
    free(discovery);
    free(metadata);
    return result;
}

BongoCatResult bongo_cat_import_session_install(
    BongoCatImportSession *session, const char *source,
    BongoCatImportReceipt *receipt, BongoCatError *error) {
    if (receipt) memset(receipt, 0, sizeof(*receipt));
    if (!session || !source) return BONGO_CAT_ERROR_ARGUMENT;
    SDL_Log("[runtime] Model import storage lock: stage=waiting path=%s",
        source);
    bongo_cat_import_storage_lock();
    SDL_Log("[runtime] Model import storage lock: stage=acquired path=%s",
        source);
    BongoCatResult result = session_install_unlocked(session, source, receipt,
        error);
    bongo_cat_import_storage_unlock();
    SDL_Log("[runtime] Model import storage lock: stage=released result=%d "
        "path=%s", (int)result, source);
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
