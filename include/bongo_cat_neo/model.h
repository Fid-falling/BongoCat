#ifndef BONGO_CAT_NEO_MODEL_H
#define BONGO_CAT_NEO_MODEL_H

#include "bongo_cat_neo/config.h"

typedef struct BongoCatNeoModelEntry {
    char id[BONGO_CAT_NEO_ID_CAP];
    char directory[BONGO_CAT_NEO_PATH_CAP];
    char adapter_directory[BONGO_CAT_NEO_PATH_CAP];
    char storage_directory[BONGO_CAT_NEO_PATH_CAP];
    char setting_file[BONGO_CAT_NEO_PATH_CAP];
    BongoCatNeoModelMode mode;
    bool preset;
    bool managed;
} BongoCatNeoModelEntry;

typedef struct BongoCatNeoModelCatalog {
    BongoCatNeoModelEntry entries[BONGO_CAT_NEO_MODEL_CAP];
    size_t count;
} BongoCatNeoModelCatalog;

typedef enum BongoCatNeoBehaviorKind {
    BONGO_CAT_NEO_BEHAVIOR_MOTION,
    BONGO_CAT_NEO_BEHAVIOR_EXPRESSION,
    BONGO_CAT_NEO_BEHAVIOR_SOUND,
    BONGO_CAT_NEO_BEHAVIOR_EFFECT
} BongoCatNeoBehaviorKind;

typedef struct BongoCatNeoBehaviorEntry {
    char id[BONGO_CAT_NEO_PATH_CAP];
    char label[BONGO_CAT_NEO_ID_CAP];
    char group[BONGO_CAT_NEO_ID_CAP];
    char sound[BONGO_CAT_NEO_PATH_CAP];
    char effect[BONGO_CAT_NEO_PATH_CAP];
    int index;
    BongoCatNeoBehaviorKind kind;
    bool momentary;
} BongoCatNeoBehaviorEntry;

typedef struct BongoCatNeoBehaviorCatalog {
    BongoCatNeoBehaviorEntry entries[BONGO_CAT_NEO_BEHAVIOR_CAP];
    size_t count;
} BongoCatNeoBehaviorCatalog;

#ifdef __cplusplus
extern "C" {
#endif

void bongo_cat_neo_models_init(BongoCatNeoModelCatalog *catalog);
BongoCatNeoResult bongo_cat_neo_models_scan(BongoCatNeoModelCatalog *catalog, const char *root,
    bool preset, BongoCatNeoError *error);
const BongoCatNeoModelEntry *bongo_cat_neo_models_find(const BongoCatNeoModelCatalog *catalog,
    const char *id);
BongoCatNeoResult bongo_cat_neo_behaviors_load(BongoCatNeoBehaviorCatalog *catalog,
    const BongoCatNeoModelEntry *model, BongoCatNeoError *error);

typedef struct BongoCatNeoLive2D BongoCatNeoLive2D;
typedef struct BongoCatNeoParameterRange { float minimum, maximum, value; } BongoCatNeoParameterRange;

BongoCatNeoLive2D *bongo_cat_neo_live2d_create(const char *asset_root, BongoCatNeoError *error);
void bongo_cat_neo_live2d_destroy(BongoCatNeoLive2D *live2d);
BongoCatNeoResult bongo_cat_neo_live2d_load(BongoCatNeoLive2D *live2d, const char *model_dir,
    const char *setting_file, bool preset, BongoCatNeoError *error);
bool bongo_cat_neo_live2d_ready(const BongoCatNeoLive2D *live2d);
void bongo_cat_neo_live2d_resize(BongoCatNeoLive2D *live2d, int width, int height);
void bongo_cat_neo_live2d_reshape(BongoCatNeoLive2D *live2d, int width, int height);
bool bongo_cat_neo_live2d_update(BongoCatNeoLive2D *live2d, float delta_seconds);
void bongo_cat_neo_live2d_draw(BongoCatNeoLive2D *live2d);
void bongo_cat_neo_live2d_set_mirror(BongoCatNeoLive2D *live2d, bool mirror);
void bongo_cat_neo_live2d_set_dragging(BongoCatNeoLive2D *live2d, float x, float y);
void bongo_cat_neo_live2d_set_mver_compatibility(BongoCatNeoLive2D *live2d, bool enabled);
bool bongo_cat_neo_live2d_set_parameter(BongoCatNeoLive2D *live2d, const char *id, float value);
bool bongo_cat_neo_live2d_parameter(BongoCatNeoLive2D *live2d, const char *id,
    BongoCatNeoParameterRange *range);
bool bongo_cat_neo_live2d_start_motion(BongoCatNeoLive2D *live2d, const char *group, int index);
bool bongo_cat_neo_live2d_set_expression(BongoCatNeoLive2D *live2d, int index);

#ifdef __cplusplus
}
#endif

#endif
