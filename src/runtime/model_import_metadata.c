#include "model_import.h"
#include "runtime.h"
#include "bongo_cat_neo/path.h"

#include <stdio.h>
#include <string.h>
#include <yyjson.h>

#define MVER_METADATA ".bongo-cat-neo-mver.json"

static bool same_family(const char *left, const char *right) {
    bool left_effect = left && (strcmp(left, "effect") == 0 ||
        strcmp(left, "effect-clear") == 0);
    bool right_effect = right && (strcmp(right, "effect") == 0 ||
        strcmp(right, "effect-clear") == 0);
    bool left_sound = left && (strcmp(left, "sound") == 0 ||
        strcmp(left, "sound-clear") == 0);
    bool right_sound = right && (strcmp(right, "sound") == 0 ||
        strcmp(right, "sound-clear") == 0);
    return (left_effect && right_effect) || (left_sound && right_sound);
}

static int sequential_index(yyjson_val *bindings, size_t index, const char *kind) {
    int result = 0;
    for (size_t before = 0; before < index; ++before) {
        yyjson_val *previous = yyjson_arr_get(bindings, before);
        const char *previous_kind = yyjson_get_str(yyjson_obj_get(previous, "kind"));
        if (same_family(previous_kind, kind)) result++;
    }
    return result;
}

static bool shortcut_present(const BongoCatNeoApp *app, const char *id) {
    for (size_t i = 0; i < app->config.behavior_shortcut_count; ++i)
        if (strcmp(app->config.behavior_shortcuts[i].id, id) == 0) return true;
    return false;
}

static bool behavior_id(char *id, size_t capacity, const char *model_id,
    yyjson_val *bindings, size_t index, yyjson_val *item) {
    const char *kind = yyjson_get_str(yyjson_obj_get(item, "kind"));
    const char *group = yyjson_get_str(yyjson_obj_get(item, "group"));
    if (!kind) return false;
    if (strcmp(kind, "sound") == 0 || strcmp(kind, "sound-clear") == 0)
        snprintf(id, capacity, "%s:sound:%d", model_id,
            sequential_index(bindings, index, kind));
    else if (strcmp(kind, "effect") == 0 || strcmp(kind, "effect-clear") == 0)
        snprintf(id, capacity, "%s:effect:%d", model_id,
            sequential_index(bindings, index, kind));
    else if (strcmp(kind, "expression") == 0)
        snprintf(id, capacity, "%s:expression:%d", model_id,
            (int)yyjson_get_int(yyjson_obj_get(item, "index")));
    else if (strcmp(kind, "motion") == 0)
        snprintf(id, capacity, "%s:motion:%s:%d", model_id, group ? group : "",
            (int)yyjson_get_int(yyjson_obj_get(item, "index")));
    else return false;
    return true;
}

void bongo_cat_neo_import_apply_metadata(BongoCatNeoApp *app, const char *model_id,
    const char *directory) {
    char path[BONGO_CAT_NEO_PATH_CAP];
    if (!bongo_cat_neo_path_join(path, sizeof(path), directory, MVER_METADATA)) return;
    yyjson_doc *document = yyjson_read_file(path, 0, NULL, NULL);
    yyjson_val *root = document ? yyjson_doc_get_root(document) : NULL;
    yyjson_val *bindings = yyjson_obj_get(root, "bindings");
    if (!yyjson_is_obj(root) || yyjson_get_int(yyjson_obj_get(root, "version")) != 1 ||
        !yyjson_is_arr(bindings)) {
        yyjson_doc_free(document);
        return;
    }
    size_t index, count; yyjson_val *item;
    yyjson_arr_foreach(bindings, index, count, item) {
        char id[BONGO_CAT_NEO_PATH_CAP];
        const char *shortcut = yyjson_get_str(yyjson_obj_get(item, "shortcut"));
        if (!shortcut || !behavior_id(id, sizeof(id), model_id, bindings, index, item) ||
            shortcut_present(app, id)) continue;
        if (app->config.behavior_shortcut_count >= BONGO_CAT_NEO_BEHAVIOR_CAP) break;
        BongoCatNeoBehaviorShortcut *value =
            &app->config.behavior_shortcuts[app->config.behavior_shortcut_count++];
        snprintf(value->id, sizeof(value->id), "%s", id);
        snprintf(value->shortcut, sizeof(value->shortcut), "%s", shortcut);
    }
    yyjson_doc_free(document);
}
