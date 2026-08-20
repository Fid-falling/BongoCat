#include "model_import.h"
#include "model_storage.h"
#include "runtime.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ImportInstall {
    char id[BONGO_CAT_ID_CAP];
    char temporary[BONGO_CAT_PATH_CAP];
    char target[BONGO_CAT_PATH_CAP];
    bool existing;
    bool committed;
} ImportInstall;

struct BongoCatImportSession {
    char root[BONGO_CAT_PATH_CAP];
    BongoCatModelCatalog *existing;
    BongoCatImportDigestCache *digests;
};

static bool custom_root(const char *data_root, char *path, size_t capacity) {
    return data_root && data_root[0] &&
        bongo_cat_path_join(path, capacity, data_root, "models") &&
        bongo_cat_path_create_directory(path);
}

static void cleanup(ImportInstall *installs, size_t count, bool committed) {
    for (size_t i = 0; i < count; ++i)
        if (!installs[i].existing)
            bongo_cat_model_remove_tree(committed && installs[i].committed
                ? installs[i].target : installs[i].temporary, NULL);
}

static bool prepare_install(const BongoCatImportCandidate *candidate,
    const BongoCatPackageMetadata *metadata,
    const BongoCatModelCatalog *existing, ImportInstall *install,
    const char *root, size_t index, BongoCatError *error) {
    const BongoCatModelEntry *duplicate =
        bongo_cat_import_find_existing_package(existing, metadata,
            candidate->mode);
    if (duplicate) {
        snprintf(install->id, sizeof(install->id), "%s", duplicate->id);
        install->existing = true;
        return true;
    }
    snprintf(install->id, sizeof(install->id), "%s", metadata->package_id);
    char temporary_name[BONGO_CAT_ID_CAP + 8];
    snprintf(temporary_name, sizeof(temporary_name), ".import-%.16s-%zu.tmp",
        metadata->content_digest, index + 1);
    BongoCatImportCandidate installed;
    if (!bongo_cat_path_join(install->temporary, sizeof(install->temporary),
        root, temporary_name) ||
        !bongo_cat_path_join(install->target, sizeof(install->target), root,
            install->id)) return false;
    if (bongo_cat_path_is_dir(install->target) ||
        bongo_cat_path_is_file(install->target)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Model package id collides with an existing directory: %s",
            install->id);
        return false;
    }
    if (!bongo_cat_import_prepare_package(candidate, install->temporary,
            &installed, error)) return false;
    char adapter[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(adapter, sizeof(adapter), install->temporary, "adapter") ||
        !bongo_cat_import_prepare_adapter(candidate, adapter, error) ||
        !bongo_cat_import_write_package(&installed, metadata,
            install->temporary, error)) return false;
    return bongo_cat_import_manifest_valid(installed.directory,
        installed.setting, error);
}

static void remember_installs(BongoCatImportSession *session,
    const BongoCatImportDiscovery *discovery,
    const BongoCatPackageMetadata *metadata, const ImportInstall *installs) {
    if (!session || !session->existing) return;
    for (size_t i = 0; i < discovery->count; ++i) {
        if (installs[i].existing || session->existing->count >=
            BONGO_CAT_MODEL_CAP) continue;
        BongoCatModelEntry *entry =
            &session->existing->entries[session->existing->count++];
        memset(entry, 0, sizeof(*entry));
        snprintf(entry->id, sizeof(entry->id), "%s", installs[i].id);
        snprintf(entry->package_id, sizeof(entry->package_id), "%s",
            installs[i].id);
        snprintf(entry->content_digest, sizeof(entry->content_digest), "%s",
            metadata[i].content_digest);
        entry->mode = discovery->candidates[i].mode;
        entry->package_schema = BONGO_CAT_MODEL_PACKAGE_SCHEMA;
    }
}

BongoCatImportSession *bongo_cat_import_session_create(const char *data_root,
    BongoCatError *error) {
    if (!data_root) return NULL;
    BongoCatImportSession *session = calloc(1, sizeof(*session));
    if (session) session->existing = calloc(1, sizeof(*session->existing));
    if (!session || !session->existing) {
        if (session) free(session->existing);
        free(session);
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Cannot allocate model import session");
        return NULL;
    }
    session->digests = bongo_cat_import_digest_cache_create();
    if (!custom_root(data_root, session->root, sizeof(session->root)) ||
        !bongo_cat_model_cleanup_imports(session->root, error)) {
        bongo_cat_import_session_destroy(session);
        return NULL;
    }
    bongo_cat_models_init(session->existing);
    if (bongo_cat_models_scan(session->existing, session->root, false,
        error) != BONGO_CAT_OK) {
        bongo_cat_import_session_destroy(session);
        return NULL;
    }
    return session;
}

void bongo_cat_import_session_destroy(BongoCatImportSession *session) {
    if (!session) return;
    bongo_cat_import_digest_cache_destroy(session->digests);
    free(session->existing);
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
    ImportInstall *installs = calloc(BONGO_CAT_IMPORT_CANDIDATE_CAP, sizeof(*installs));
    BongoCatPackageMetadata *metadata = calloc(BONGO_CAT_IMPORT_CANDIDATE_CAP,
        sizeof(*metadata));
    if (!discovery || !installs || !metadata) {
        free(discovery); free(installs); free(metadata);
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Cannot allocate model import workspace");
        return BONGO_CAT_ERROR_MEMORY;
    }
    if (!bongo_cat_import_discover(source_directory, discovery, error)) {
        free(discovery); free(installs); free(metadata);
        return BONGO_CAT_ERROR_FORMAT;
    }
    if (!bongo_cat_import_prepare_package_metadata_cached(discovery, metadata,
        session->digests, error)) {
        BongoCatResult result = error && error->code ? error->code :
            BONGO_CAT_ERROR_IO;
        free(discovery); free(installs); free(metadata);
        return result;
    }
    for (size_t i = 0; i < discovery->count; ++i) {
        if (prepare_install(&discovery->candidates[i], &metadata[i],
            session->existing, &installs[i], session->root, i, error))
            continue;
        cleanup(installs, i + 1, false);
        BongoCatResult result = error && error->code == BONGO_CAT_ERROR_FORMAT
            ? BONGO_CAT_ERROR_FORMAT : BONGO_CAT_ERROR_IO;
        free(discovery); free(installs); free(metadata);
        return result;
    }
    for (size_t i = 0; i < discovery->count; ++i) {
        if (installs[i].existing) continue;
        if (!bongo_cat_path_rename(installs[i].temporary, installs[i].target)) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
                "Cannot finish model import: %s", SDL_GetError());
            cleanup(installs, discovery->count, true);
            free(discovery); free(installs); free(metadata);
            return BONGO_CAT_ERROR_IO;
        }
        installs[i].committed = true;
    }
    if (receipt) {
        receipt->count = discovery->count;
        for (size_t i = 0; i < discovery->count; ++i) {
            snprintf(receipt->ids[i], sizeof(receipt->ids[i]), "%s", installs[i].id);
            receipt->installed[i] = !installs[i].existing;
            if (receipt->installed[i]) receipt->installed_count++;
        }
    }
    remember_installs(session, discovery, metadata, installs);
    free(discovery); free(installs); free(metadata);
    return BONGO_CAT_OK;
}

BongoCatResult bongo_cat_import_install(const char *source, const char *data_root,
    BongoCatImportReceipt *receipt, BongoCatError *error) {
    if (receipt) memset(receipt, 0, sizeof(*receipt));
    if (!source || !data_root) return BONGO_CAT_ERROR_ARGUMENT;
    BongoCatImportSession *session = bongo_cat_import_session_create(
        data_root, error);
    if (!session) return error && error->code ? error->code :
        BONGO_CAT_ERROR_IO;
    BongoCatResult result = bongo_cat_import_session_install(session,
        source, receipt, error);
    bongo_cat_import_session_destroy(session);
    return result;
}

#ifdef BONGO_CAT_HAS_CUBISM
static void remove_receipt(const char *data_root,
    const BongoCatImportReceipt *receipt) {
    char root[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
    if (!receipt || !custom_root(data_root, root, sizeof(root))) return;
    for (size_t i = 0; i < receipt->count; ++i)
        if (receipt->installed[i] &&
            bongo_cat_path_join(path, sizeof(path), root, receipt->ids[i]))
            bongo_cat_model_remove_tree(path, NULL);
}
#endif

BongoCatResult bongo_cat_app_import_model(BongoCatApp *app, const char *source,
    BongoCatError *error) {
    if (!app || !source) return BONGO_CAT_ERROR_ARGUMENT;
    BongoCatImportReceipt receipt;
    BongoCatResult result = bongo_cat_import_install(source, app->data_root,
        &receipt, error);
    if (result != BONGO_CAT_OK) {
        if (error && !error->message[0]) bongo_cat_error_set(error, result,
            "Model import failed while installing: %s", source);
        return result;
    }
    char previous[BONGO_CAT_PATH_CAP];
    snprintf(previous, sizeof(previous), "%s", app->session.active_model_id);
    size_t preferred = 0;
    for (size_t i = 0; i < receipt.count; ++i)
        if (receipt.installed[i]) { preferred = i; break; }
    const char *imported_id = receipt.count ? receipt.ids[preferred] : NULL;
    bongo_cat_app_refresh_installed_models(app);
    if (imported_id && bongo_cat_app_select_model(app, imported_id))
        return BONGO_CAT_OK;
#ifndef BONGO_CAT_HAS_CUBISM
    const BongoCatModelEntry *entry = imported_id ?
        bongo_cat_models_find(&app->models, imported_id) : NULL;
    if (entry) {
        snprintf(app->session.active_model_id, sizeof(app->session.active_model_id),
            "%s", imported_id);
        app->loaded_mode = entry->mode;
    }
    return BONGO_CAT_OK;
#else
    remove_receipt(app->data_root, &receipt);
    bongo_cat_app_refresh_installed_models(app);
    if (previous[0]) bongo_cat_app_select_model(app, previous);
    bongo_cat_error_set(error, BONGO_CAT_ERROR_CUBISM,
        "Model import was rolled back because the Live2D model could not be loaded");
    return BONGO_CAT_ERROR_CUBISM;
#endif
}
