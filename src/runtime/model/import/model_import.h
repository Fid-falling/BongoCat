#ifndef BONGO_CAT_MODEL_IMPORT_H
#define BONGO_CAT_MODEL_IMPORT_H

#include "bongo_cat/common.h"
#include "bongo_cat/model.h"

#define BONGO_CAT_IMPORT_CANDIDATE_CAP 16

typedef enum BongoCatImportFormat {
    BONGO_CAT_IMPORT_TAURI,
    BONGO_CAT_IMPORT_MVER,
    BONGO_CAT_IMPORT_MVER_PATCH
} BongoCatImportFormat;

typedef struct BongoCatImportCandidate {
    char directory[BONGO_CAT_PATH_CAP];
    char setting[BONGO_CAT_PATH_CAP];
    char assets[BONGO_CAT_PATH_CAP];
    char package_root[BONGO_CAT_PATH_CAP];
    char patch_root[BONGO_CAT_PATH_CAP];
    char config[BONGO_CAT_PATH_CAP];
    char overrides[BONGO_CAT_PATH_CAP];
    BongoCatModelMode mode;
    BongoCatImportFormat format;
    bool gamepad_buttons;
} BongoCatImportCandidate;

typedef struct BongoCatImportDiscovery {
    BongoCatImportCandidate candidates[BONGO_CAT_IMPORT_CANDIDATE_CAP];
    size_t count;
    int depth;
    bool ambiguous;
} BongoCatImportDiscovery;

typedef struct BongoCatImportReceipt {
    char ids[BONGO_CAT_IMPORT_CANDIDATE_CAP][BONGO_CAT_ID_CAP];
    bool installed[BONGO_CAT_IMPORT_CANDIDATE_CAP];
    size_t count;
    size_t installed_count;
} BongoCatImportReceipt;

typedef struct BongoCatPackageMetadata {
    char package_id[BONGO_CAT_ID_CAP];
    char content_digest[65];
    char family_id[BONGO_CAT_ID_CAP];
    char display_name[BONGO_CAT_ID_CAP];
    char source_name[BONGO_CAT_ID_CAP];
    uint32_t capabilities;
} BongoCatPackageMetadata;

typedef BongoCatResult (*BongoCatImportVisitor)(void *userdata,
    const char *source, BongoCatImportDiscovery *discovery, BongoCatError *error);

typedef struct BongoCatApp BongoCatApp;

bool bongo_cat_import_discover(const char *source, BongoCatImportDiscovery *discovery,
    BongoCatError *error);
int bongo_cat_import_mver_discover(const char *source,
    BongoCatImportDiscovery *discovery, BongoCatError *error);
int bongo_cat_import_mver_discover_exact(const char *source,
    BongoCatImportDiscovery *discovery, BongoCatError *error);
int bongo_cat_import_mver_patch_discover(const char *source,
    BongoCatImportDiscovery *discovery, BongoCatError *error);
int bongo_cat_import_mver_patch_discover_exact(const char *source,
    BongoCatImportDiscovery *discovery, BongoCatError *error);
int bongo_cat_import_tauri_discover_exact(const char *source,
    BongoCatImportDiscovery *discovery, BongoCatError *error);
bool bongo_cat_import_manifest_valid(const char *root, const char *setting,
    BongoCatError *error);
bool bongo_cat_import_tauri_add_candidate(BongoCatImportDiscovery *discovery,
    const char *directory, const char *setting);
bool bongo_cat_import_mver_assets(const BongoCatImportCandidate *candidate,
    const char *target, BongoCatError *error);
bool bongo_cat_import_adapter_metadata(const BongoCatImportCandidate *candidate,
    const char *target, BongoCatError *error);
bool bongo_cat_import_write_report(const BongoCatImportCandidate *candidate,
    const char *target, BongoCatError *error);
bool bongo_cat_import_prepare_adapter(const BongoCatImportCandidate *candidate,
    const char *target, BongoCatError *error);
bool bongo_cat_import_prepare_package(const BongoCatImportCandidate *candidate,
    const char *target, BongoCatImportCandidate *installed, BongoCatError *error);
bool bongo_cat_import_write_package(const BongoCatImportCandidate *candidate,
    const BongoCatPackageMetadata *metadata, const char *target,
    BongoCatError *error);
bool bongo_cat_import_candidate_digest(const BongoCatImportCandidate *candidate,
    char output[65], BongoCatError *error);
bool bongo_cat_import_candidate_inspect(const BongoCatImportCandidate *candidate,
    char output[65], bool *placeholder, BongoCatError *error);
bool bongo_cat_import_prepare_package_metadata(
    BongoCatImportDiscovery *discovery,
    BongoCatPackageMetadata *metadata, BongoCatError *error);
const BongoCatModelEntry *bongo_cat_import_find_existing_package(
    const BongoCatModelCatalog *catalog,
    const BongoCatPackageMetadata *metadata, BongoCatModelMode mode);
void bongo_cat_import_describe_nearby_entry(BongoCatModelEntry *entry,
    const BongoCatImportCandidate *candidate, const char *id,
    const char *identity, const char *source_hash, const char *source,
    const char *adapter);
void bongo_cat_import_apply_metadata(BongoCatApp *app, const char *model_id,
    const char *directory);
bool bongo_cat_import_render_options(const char *directory,
    BongoCatLive2DRenderOptions *options);
BongoCatResult bongo_cat_import_scan(const char *root,
    BongoCatImportVisitor visitor, void *userdata, BongoCatError *error);
BongoCatResult bongo_cat_import_scan_budget(const char *root,
    BongoCatImportVisitor visitor, void *userdata, uint64_t budget_ns,
    BongoCatError *error);
BongoCatResult bongo_cat_import_source_directory(const char *source,
    char *directory, size_t capacity, BongoCatError *error);
BongoCatResult bongo_cat_import_nearby_root(BongoCatApp *app,
    const char *root, BongoCatError *error);
BongoCatResult bongo_cat_import_nearby_scan(BongoCatApp *app,
    const char *root, BongoCatError *error);
BongoCatResult bongo_cat_import_nearby(BongoCatApp *app,
    const char *root, BongoCatError *error);
BongoCatResult bongo_cat_import_install(const char *source,
    const char *data_root, BongoCatImportReceipt *receipt,
    BongoCatError *error);

#endif
