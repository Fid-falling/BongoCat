#ifndef BONGO_CAT_MODEL_H
#define BONGO_CAT_MODEL_H

#include "bongo_cat/config.h"

typedef struct BongoCatModelEntry {
    char id[BONGO_CAT_ID_CAP];
    char directory[BONGO_CAT_PATH_CAP];
    char adapter_directory[BONGO_CAT_PATH_CAP];
    char storage_directory[BONGO_CAT_PATH_CAP];
    char setting_file[BONGO_CAT_PATH_CAP];
    BongoCatModelMode mode;
    bool preset;
    bool managed;
} BongoCatModelEntry;

typedef struct BongoCatModelCatalog {
    BongoCatModelEntry entries[BONGO_CAT_MODEL_CAP];
    size_t count;
} BongoCatModelCatalog;

typedef enum BongoCatBehaviorKind {
    BONGO_CAT_BEHAVIOR_MOTION,
    BONGO_CAT_BEHAVIOR_EXPRESSION,
    BONGO_CAT_BEHAVIOR_SOUND,
    BONGO_CAT_BEHAVIOR_EFFECT
} BongoCatBehaviorKind;

typedef struct BongoCatBehaviorEntry {
    char id[BONGO_CAT_PATH_CAP];
    char label[BONGO_CAT_ID_CAP];
    char group[BONGO_CAT_ID_CAP];
    char sound[BONGO_CAT_PATH_CAP];
    char effect[BONGO_CAT_PATH_CAP];
    int index;
    BongoCatBehaviorKind kind;
    bool momentary;
} BongoCatBehaviorEntry;

typedef struct BongoCatBehaviorCatalog {
    BongoCatBehaviorEntry entries[BONGO_CAT_BEHAVIOR_CAP];
    size_t count;
} BongoCatBehaviorCatalog;

#ifdef __cplusplus
extern "C" {
#endif

void bongo_cat_models_init(BongoCatModelCatalog *catalog);
BongoCatResult bongo_cat_models_scan(BongoCatModelCatalog *catalog, const char *root,
    bool preset, BongoCatError *error);
const BongoCatModelEntry *bongo_cat_models_find(const BongoCatModelCatalog *catalog,
    const char *id);
BongoCatResult bongo_cat_behaviors_load(BongoCatBehaviorCatalog *catalog,
    const BongoCatModelEntry *model, BongoCatError *error);

typedef struct BongoCatLive2D BongoCatLive2D;
typedef struct BongoCatParameterRange { float minimum, maximum, value; } BongoCatParameterRange;

BongoCatLive2D *bongo_cat_live2d_create(const char *asset_root, BongoCatError *error);
void bongo_cat_live2d_destroy(BongoCatLive2D *live2d);
BongoCatResult bongo_cat_live2d_load(BongoCatLive2D *live2d, const char *model_dir,
    const char *setting_file, bool preset, BongoCatError *error);
bool bongo_cat_live2d_ready(const BongoCatLive2D *live2d);
void bongo_cat_live2d_resize(BongoCatLive2D *live2d, int width, int height);
void bongo_cat_live2d_reshape(BongoCatLive2D *live2d, int width, int height);
bool bongo_cat_live2d_update(BongoCatLive2D *live2d, float delta_seconds);
void bongo_cat_live2d_draw(BongoCatLive2D *live2d);
void bongo_cat_live2d_set_mirror(BongoCatLive2D *live2d, bool mirror);
void bongo_cat_live2d_set_dragging(BongoCatLive2D *live2d, float x, float y);
void bongo_cat_live2d_set_mver_compatibility(BongoCatLive2D *live2d, bool enabled);
bool bongo_cat_live2d_set_parameter(BongoCatLive2D *live2d, const char *id, float value);
bool bongo_cat_live2d_parameter(BongoCatLive2D *live2d, const char *id,
    BongoCatParameterRange *range);
bool bongo_cat_live2d_start_motion(BongoCatLive2D *live2d, const char *group, int index);
bool bongo_cat_live2d_set_expression(BongoCatLive2D *live2d, int index);

#ifdef __cplusplus
}
#endif

#endif
