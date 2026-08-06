#include "model_import.h"
#include "bongo_cat/json.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <yyjson.h>

const char *test_mver_pointer_config(void) {
    return "{\"decoration\":{\"l2d_correct\":1.987,"
        "\"l2d_offset\":[0,-0.005],\"l2d_horizontal_flip\":true,"
        "\"window_size\":[1400,1400],\"offsetX\":[10,11],"
        "\"offsetY\":[-10,-65],\"scalar\":[1,1],"
        "\"armLineColor\":[1,2,3],\"hand_offset\":[4,5],"
        "\"leftHanded\":true,\"mouse_force_move\":true,"
        "\"mouse_speed\":1.25},\"workarea\":{\"workarea\":true,"
        "\"top_left\":[100,200],\"right_bottom\":[2100,1400]},"
        "\"standard\":{\"l2d\":false,\"mouse\":false,"
        "\"keyboard\":[[65]],\"hand\":[[65]],"
        "\"l2d_expression\":[[65],[66]]}}";
}

bool test_mver_pointer_fixture_assets(const char *standard,
    const char *source) {
    static const char *const names[] = {
        "arm.png", "tablet.png", "tablet_left.png", "tablet_right.png"
    };
    char path[BONGO_CAT_PATH_CAP];
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); ++index)
        if (!bongo_cat_path_join(path, sizeof(path), standard, names[index]) ||
            !SDL_CopyFile(source, path)) return false;
    return true;
}

bool test_mver_pointer_adapter(const char *adapter) {
    char path[BONGO_CAT_PATH_CAP];
    bool files = bongo_cat_path_join(path, sizeof(path), adapter,
        "resources/mver-pointer/arm.png") && bongo_cat_path_is_file(path) &&
        bongo_cat_path_join(path, sizeof(path), adapter,
            "resources/mver-pointer/tablet.png") && bongo_cat_path_is_file(path) &&
        bongo_cat_path_join(path, sizeof(path), adapter, ".bongo-cat-mver.json");
    yyjson_doc *metadata = files ? bongo_cat_json_read_file(path, 0, NULL) : NULL;
    yyjson_val *pointer = metadata ? yyjson_obj_get(
        yyjson_doc_get_root(metadata), "standardPointer") : NULL;
    bool valid = yyjson_is_obj(pointer) &&
        yyjson_get_bool(yyjson_obj_get(pointer, "enabled")) &&
        !yyjson_get_bool(yyjson_obj_get(pointer, "mouse")) &&
        yyjson_get_num(yyjson_obj_get(pointer, "offsetY")) == -65.0 &&
        yyjson_get_num(yyjson_obj_get(pointer, "handOffsetX")) == 4.0 &&
        yyjson_get_int(yyjson_obj_get(pointer, "lineBlue")) == 3;
    yyjson_doc_free(metadata);
    BongoCatLive2DRenderOptions render = {0};
    return valid && bongo_cat_import_mver_render_options(adapter, &render) &&
        render.pointer_left_handed && render.mouse_force_move &&
        render.mouse_speed > 1.249f && render.mouse_speed < 1.251f;
}
