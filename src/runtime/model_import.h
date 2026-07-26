#ifndef BONGO_CAT_NEO_MODEL_IMPORT_H
#define BONGO_CAT_NEO_MODEL_IMPORT_H

#include "bongo_cat_neo/common.h"
#include "bongo_cat_neo/model.h"

#define BONGO_CAT_NEO_IMPORT_CANDIDATE_CAP 16

typedef enum BongoCatNeoImportFormat {
    BONGO_CAT_NEO_IMPORT_TAURI,
    BONGO_CAT_NEO_IMPORT_MVER,
    BONGO_CAT_NEO_IMPORT_MVER_PATCH
} BongoCatNeoImportFormat;

typedef struct BongoCatNeoImportCandidate {
    char directory[BONGO_CAT_NEO_PATH_CAP];
    char setting[BONGO_CAT_NEO_PATH_CAP];
    char assets[BONGO_CAT_NEO_PATH_CAP];
    char package_root[BONGO_CAT_NEO_PATH_CAP];
    char patch_root[BONGO_CAT_NEO_PATH_CAP];
    char config[BONGO_CAT_NEO_PATH_CAP];
    char overrides[BONGO_CAT_NEO_PATH_CAP];
    BongoCatNeoModelMode mode;
    BongoCatNeoImportFormat format;
    bool gamepad_buttons;
    bool portable_compat;
} BongoCatNeoImportCandidate;

typedef struct BongoCatNeoImportDiscovery {
    BongoCatNeoImportCandidate candidates[BONGO_CAT_NEO_IMPORT_CANDIDATE_CAP];
    size_t count;
    int depth;
    bool ambiguous;
} BongoCatNeoImportDiscovery;

typedef BongoCatNeoResult (*BongoCatNeoPortableVisitor)(void *userdata,
    const char *source, BongoCatNeoImportDiscovery *discovery, BongoCatNeoError *error);

typedef struct BongoCatNeoApp BongoCatNeoApp;

bool bongo_cat_neo_import_discover(const char *source, BongoCatNeoImportDiscovery *discovery,
    BongoCatNeoError *error);
int bongo_cat_neo_import_mver_discover(const char *source,
    BongoCatNeoImportDiscovery *discovery, BongoCatNeoError *error);
int bongo_cat_neo_import_mver_discover_exact(const char *source,
    BongoCatNeoImportDiscovery *discovery, BongoCatNeoError *error);
int bongo_cat_neo_import_mver_patch_discover(const char *source,
    BongoCatNeoImportDiscovery *discovery, BongoCatNeoError *error);
bool bongo_cat_neo_import_manifest_valid(const char *root, const char *setting,
    BongoCatNeoError *error);
bool bongo_cat_neo_import_mver_assets(const BongoCatNeoImportCandidate *candidate,
    const char *target, BongoCatNeoError *error);
bool bongo_cat_neo_import_mver_metadata(const BongoCatNeoImportCandidate *candidate,
    const char *target, BongoCatNeoError *error);
bool bongo_cat_neo_import_write_report(const BongoCatNeoImportCandidate *candidate,
    const char *target, BongoCatNeoError *error);
bool bongo_cat_neo_import_prepare_adapter(const BongoCatNeoImportCandidate *candidate,
    const char *target, BongoCatNeoError *error);
bool bongo_cat_neo_import_prepare_package(const BongoCatNeoImportCandidate *candidate,
    const char *target, BongoCatNeoImportCandidate *installed, BongoCatNeoError *error);
bool bongo_cat_neo_import_write_package(const BongoCatNeoImportCandidate *candidate,
    const char *target, BongoCatNeoError *error);
void bongo_cat_neo_import_apply_metadata(BongoCatNeoApp *app, const char *model_id,
    const char *directory);
BongoCatNeoResult bongo_cat_neo_import_portable_mver(BongoCatNeoApp *app,
    const char *root, BongoCatNeoError *error);
BongoCatNeoResult bongo_cat_neo_import_portable_scan(const char *root,
    BongoCatNeoPortableVisitor visitor, void *userdata, BongoCatNeoError *error);

#endif
