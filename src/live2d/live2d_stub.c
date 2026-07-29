#include "bongo_cat_neo/model.h"
#include "../runtime/model_import.h"

#include <stdlib.h>

struct BongoCatNeoLive2D { int width; int height; bool loaded; };

BongoCatNeoLive2D *bongo_cat_neo_live2d_create(const char *asset_root, BongoCatNeoError *error) {
    (void)asset_root;
    BongoCatNeoLive2D *value = calloc(1, sizeof(*value));
    if (!value) bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_MEMORY, "Cannot allocate Live2D runtime");
    return value;
}

void bongo_cat_neo_live2d_destroy(BongoCatNeoLive2D *live2d) { free(live2d); }

BongoCatNeoResult bongo_cat_neo_live2d_load(BongoCatNeoLive2D *live2d, const char *model_dir,
    const char *setting_file, bool preset, BongoCatNeoError *error) {
    (void)preset;
    if (!live2d || !model_dir || !setting_file) return BONGO_CAT_NEO_ERROR_ARGUMENT;
    if (!bongo_cat_neo_import_manifest_valid(model_dir, setting_file, error))
        return BONGO_CAT_NEO_ERROR_FORMAT;
    live2d->loaded = true;
    return BONGO_CAT_NEO_OK;
}

bool bongo_cat_neo_live2d_ready(const BongoCatNeoLive2D *live2d) {
    return live2d && live2d->loaded;
}

void bongo_cat_neo_live2d_resize(BongoCatNeoLive2D *live2d, int width, int height) {
    if (!live2d) return;
    live2d->width = width;
    live2d->height = height;
}
void bongo_cat_neo_live2d_reshape(BongoCatNeoLive2D *live2d, int width, int height) {
    bongo_cat_neo_live2d_resize(live2d, width, height);
}

bool bongo_cat_neo_live2d_update(BongoCatNeoLive2D *live2d, float delta_seconds) {
    (void)live2d; (void)delta_seconds; return false;
}
void bongo_cat_neo_live2d_draw(BongoCatNeoLive2D *live2d) { (void)live2d; }
void bongo_cat_neo_live2d_set_mirror(BongoCatNeoLive2D *live2d, bool mirror) {
    (void)live2d; (void)mirror;
}
void bongo_cat_neo_live2d_set_dragging(BongoCatNeoLive2D *live2d, float x, float y) {
    (void)live2d; (void)x; (void)y;
}
void bongo_cat_neo_live2d_set_mver_compatibility(BongoCatNeoLive2D *live2d, bool enabled) {
    (void)live2d; (void)enabled;
}
bool bongo_cat_neo_live2d_set_parameter(BongoCatNeoLive2D *live2d, const char *id, float value) {
    (void)live2d; (void)id; (void)value; return false;
}
bool bongo_cat_neo_live2d_parameter(BongoCatNeoLive2D *value, const char *id, BongoCatNeoParameterRange *range) {
    (void)value; (void)id; (void)range; return false;
}
bool bongo_cat_neo_live2d_start_motion(BongoCatNeoLive2D *value, const char *group, int index) {
    (void)value; (void)group; (void)index; return false;
}
bool bongo_cat_neo_live2d_set_expression(BongoCatNeoLive2D *value, int index) {
    (void)value; (void)index; return false;
}
