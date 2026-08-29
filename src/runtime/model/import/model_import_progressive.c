#include "model_import.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ProgressiveBase {
    char directory[BONGO_CAT_PATH_CAP];
    char setting[BONGO_CAT_PATH_CAP];
    BongoCatModelMode mode;
} ProgressiveBase;

typedef struct ProgressiveUnit {
    char source[BONGO_CAT_PATH_CAP];
    bool patch_only;
} ProgressiveUnit;

typedef struct ProgressiveScan {
    ProgressiveUnit units[BONGO_CAT_MODEL_CAP];
    ProgressiveBase bases[BONGO_CAT_MODEL_CAP];
    size_t unit_count;
    size_t base_count;
    size_t model_count;
} ProgressiveScan;

static int discover_exact(const char *source,
    BongoCatImportDiscovery *discovery, BongoCatError *error) {
    memset(discovery, 0, sizeof(*discovery));
    int found = bongo_cat_import_mver_discover_exact(source, discovery, error);
    if (found) return found;
    memset(discovery, 0, sizeof(*discovery));
    found = bongo_cat_import_mver_patch_discover_exact(source, discovery,
        error);
    if (found) return found;
    memset(discovery, 0, sizeof(*discovery));
    return bongo_cat_import_tauri_discover_exact(source, discovery, error);
}

static BongoCatResult collect_unit(void *userdata, const char *source,
    BongoCatImportDiscovery *discovery, BongoCatError *error) {
    ProgressiveScan *scan = userdata;
    if (scan->unit_count >= BONGO_CAT_MODEL_CAP ||
        discovery->count > BONGO_CAT_MODEL_CAP - scan->model_count) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Selected folder contains too many model variants");
        return BONGO_CAT_ERROR_FORMAT;
    }
    ProgressiveUnit *unit = &scan->units[scan->unit_count++];
    snprintf(unit->source, sizeof(unit->source), "%s", source);
    unit->patch_only = discovery->count > 0;
    scan->model_count += discovery->count;
    for (size_t i = 0; i < discovery->count; ++i) {
        const BongoCatImportCandidate *candidate = &discovery->candidates[i];
        if (candidate->format != BONGO_CAT_IMPORT_MVER_PATCH)
            unit->patch_only = false;
        if (candidate->format != BONGO_CAT_IMPORT_MVER) continue;
        if (scan->base_count >= BONGO_CAT_MODEL_CAP) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
                "Selected folder contains too many model variants");
            return BONGO_CAT_ERROR_FORMAT;
        }
        ProgressiveBase *base = &scan->bases[scan->base_count++];
        snprintf(base->directory, sizeof(base->directory), "%s",
            candidate->directory);
        snprintf(base->setting, sizeof(base->setting), "%s",
            candidate->setting);
        base->mode = candidate->mode;
    }
    return BONGO_CAT_OK;
}

static bool has_base(const ProgressiveScan *scan,
    const BongoCatImportCandidate *patch) {
    for (size_t i = 0; i < scan->base_count; ++i) {
        const ProgressiveBase *base = &scan->bases[i];
        if (base->mode == patch->mode &&
            strcmp(base->directory, patch->directory) == 0 &&
            strcmp(base->setting, patch->setting) == 0) return true;
    }
    return false;
}

static bool redundant_patch(const ProgressiveScan *scan,
    const ProgressiveUnit *unit) {
    if (!unit->patch_only) return false;
    BongoCatImportDiscovery discovery;
    BongoCatError ignored = {0};
    if (discover_exact(unit->source, &discovery, &ignored) <= 0 ||
        !discovery.count) return false;
    for (size_t i = 0; i < discovery.count; ++i)
        if (!has_base(scan, &discovery.candidates[i])) return false;
    return true;
}

static BongoCatResult install_one(BongoCatImportSession *session,
    const char *source, BongoCatImportReceiptCallback callback,
    void *userdata, BongoCatError *error) {
    BongoCatImportReceipt receipt = {0};
    BongoCatResult result = bongo_cat_import_session_install(session, source,
        &receipt, error);
    if (result == BONGO_CAT_OK && receipt.count && callback)
        callback(userdata, &receipt);
    return result;
}

BongoCatResult bongo_cat_import_session_install_progressive(
    BongoCatImportSession *session, const char *source,
    BongoCatImportReceiptCallback callback, void *userdata,
    BongoCatError *error) {
    if (!session || !source) return BONGO_CAT_ERROR_ARGUMENT;
    char directory[BONGO_CAT_PATH_CAP];
    BongoCatResult normalized = bongo_cat_import_source_directory(source,
        directory, sizeof(directory), error);
    if (normalized != BONGO_CAT_OK) return normalized;

    BongoCatImportDiscovery exact;
    BongoCatError exact_error = {0};
    int exact_result = discover_exact(directory, &exact, &exact_error);
    if (exact_result > 0)
        return install_one(session, directory, callback, userdata, error);
    if (exact_result < 0) {
        if (error) *error = exact_error;
        return exact_error.code ? exact_error.code : BONGO_CAT_ERROR_FORMAT;
    }

    ProgressiveScan *scan = calloc(1, sizeof(*scan));
    if (!scan) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Cannot allocate progressive model import scan");
        return BONGO_CAT_ERROR_MEMORY;
    }
    BongoCatResult scanned = bongo_cat_import_scan(directory, collect_unit,
        scan, error);
    if (scanned != BONGO_CAT_OK || !scan->unit_count) {
        free(scan);
        return scanned != BONGO_CAT_OK ? scanned : install_one(session,
            directory, callback, userdata, error);
    }

    BongoCatResult first_failure = BONGO_CAT_OK;
    BongoCatError first_error = {0};
    for (size_t i = 0; i < scan->unit_count; ++i) {
        if (redundant_patch(scan, &scan->units[i])) continue;
        BongoCatError local = {0};
        BongoCatResult result = install_one(session, scan->units[i].source,
            callback, userdata, &local);
        if (result != BONGO_CAT_OK && first_failure == BONGO_CAT_OK) {
            first_failure = result;
            first_error = local;
        }
    }
    free(scan);
    if (first_failure != BONGO_CAT_OK && error) *error = first_error;
    return first_failure;
}
