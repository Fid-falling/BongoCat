#include "bongo_cat_neo/file.h"
#include "bongo_cat_neo/model.h"
#include "bongo_cat_neo/path.h"

#include <stdio.h>
#include <string.h>
#include <yyjson.h>

static bool add_behavior(BongoCatNeoBehaviorCatalog *catalog, const BongoCatNeoModelEntry *model,
    BongoCatNeoBehaviorKind kind, const char *group, int index, const char *label,
    const char *sound) {
    if (catalog->count >= BONGO_CAT_NEO_BEHAVIOR_CAP) return false;
    BongoCatNeoBehaviorEntry *entry = &catalog->entries[catalog->count++];
    entry->kind = kind;
    entry->index = index;
    snprintf(entry->group, sizeof(entry->group), "%s", group ? group : "");
    snprintf(entry->label, sizeof(entry->label), "%s", label ? label : "");
    if (kind == BONGO_CAT_NEO_BEHAVIOR_MOTION)
        snprintf(entry->id, sizeof(entry->id), "%s:motion:%s:%d",
            model->id, group ? group : "", index);
    else if (kind == BONGO_CAT_NEO_BEHAVIOR_EXPRESSION)
        snprintf(entry->id, sizeof(entry->id), "%s:expression:%d", model->id, index);
    else snprintf(entry->id, sizeof(entry->id), "%s:sound:%d", model->id, index);
    if (sound && *sound)
        bongo_cat_neo_path_join(entry->sound, sizeof(entry->sound), model->directory, sound);
    return true;
}

static bool read_motions(BongoCatNeoBehaviorCatalog *catalog, const BongoCatNeoModelEntry *model,
    yyjson_val *motions) {
    if (!yyjson_is_obj(motions)) return true;
    size_t group_index, group_count;
    yyjson_val *group_key, *items;
    yyjson_obj_foreach(motions, group_index, group_count, group_key, items) {
        if (!yyjson_is_arr(items)) continue;
        const char *group = yyjson_get_str(group_key);
        size_t index, count; yyjson_val *item;
        yyjson_arr_foreach(items, index, count, item) {
            const char *sound = yyjson_get_str(yyjson_obj_get(item, "Sound"));
            char label[BONGO_CAT_NEO_ID_CAP];
            snprintf(label, sizeof(label), "%s %zu", group, index + 1);
            if (!add_behavior(catalog, model, BONGO_CAT_NEO_BEHAVIOR_MOTION, group,
                (int)index, label, sound)) return false;
        }
    }
    return true;
}

static bool read_expressions(BongoCatNeoBehaviorCatalog *catalog,
    const BongoCatNeoModelEntry *model, yyjson_val *expressions) {
    if (!yyjson_is_arr(expressions)) return true;
    size_t index, count; yyjson_val *item;
    yyjson_arr_foreach(expressions, index, count, item) {
        const char *name = yyjson_get_str(yyjson_obj_get(item, "Name"));
        char label[BONGO_CAT_NEO_ID_CAP];
        snprintf(label, sizeof(label), "%s", name ? name : "Expression");
        if (!add_behavior(catalog, model, BONGO_CAT_NEO_BEHAVIOR_EXPRESSION, NULL,
            (int)index, label, NULL)) return false;
    }
    return true;
}

static bool read_mver_sounds(BongoCatNeoBehaviorCatalog *catalog,
    const BongoCatNeoModelEntry *model) {
    char path[BONGO_CAT_NEO_PATH_CAP];
    if (!bongo_cat_neo_path_join(path, sizeof(path), model->directory,
        ".bongo-cat-neo-mver.json")) return false;
    yyjson_doc *document = yyjson_read_file(path, 0, NULL, NULL);
    if (!document) return true;
    yyjson_val *items = yyjson_obj_get(yyjson_doc_get_root(document), "bindings");
    size_t index, count; yyjson_val *item;
    int sound_index = 0; bool ok = true;
    yyjson_arr_foreach(items, index, count, item) {
        const char *kind = yyjson_get_str(yyjson_obj_get(item, "kind"));
        if (!kind || strcmp(kind, "sound") != 0) continue;
        const char *sound = yyjson_get_str(yyjson_obj_get(item, "sound"));
        char label[BONGO_CAT_NEO_ID_CAP];
        snprintf(label, sizeof(label), "Sound %d", sound_index + 1);
        if (!add_behavior(catalog, model, BONGO_CAT_NEO_BEHAVIOR_SOUND, NULL,
            sound_index++, label, sound)) { ok = false; break; }
    }
    yyjson_doc_free(document);
    return ok;
}

BongoCatNeoResult bongo_cat_neo_behaviors_load(BongoCatNeoBehaviorCatalog *catalog,
    const BongoCatNeoModelEntry *model, BongoCatNeoError *error) {
    if (!catalog || !model) return BONGO_CAT_NEO_ERROR_ARGUMENT;
    memset(catalog, 0, sizeof(*catalog));
    char path[BONGO_CAT_NEO_PATH_CAP];
    if (!bongo_cat_neo_path_join(path, sizeof(path), model->directory, model->setting_file))
        return BONGO_CAT_NEO_ERROR_FORMAT;
    yyjson_read_err json_error = {0};
    FILE *file = bongo_cat_neo_file_open(path, "rb");
    yyjson_doc *document = file ? yyjson_read_fp(file, 0, NULL, &json_error) : NULL;
    if (file) fclose(file);
    if (!document) {
        bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_FORMAT, "Cannot read model setting: %s",
            json_error.msg ? json_error.msg : "cannot open file");
        return BONGO_CAT_NEO_ERROR_FORMAT;
    }
    yyjson_val *references = yyjson_obj_get(yyjson_doc_get_root(document), "FileReferences");
    bool ok = read_motions(catalog, model, yyjson_obj_get(references, "Motions")) &&
        read_expressions(catalog, model, yyjson_obj_get(references, "Expressions")) &&
        read_mver_sounds(catalog, model);
    yyjson_doc_free(document);
    if (!ok) {
        bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_FORMAT, "Too many model behaviors");
        return BONGO_CAT_NEO_ERROR_FORMAT;
    }
    return BONGO_CAT_NEO_OK;
}
