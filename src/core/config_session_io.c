#include "config_internal.h"
#include "bongo_cat/path.h"

#include <stdio.h>

#define SESSION_FORMAT "bongocat/session"

static bool get_bool(yyjson_val *obj, const char *key, bool fallback) {
    yyjson_val *value = yyjson_obj_get(obj, key);
    return yyjson_is_bool(value) ? yyjson_get_bool(value) : fallback;
}
static int get_int(yyjson_val *obj, const char *key, int fallback) {
    yyjson_val *value = yyjson_obj_get(obj, key);
    return yyjson_is_num(value) ? (int)yyjson_get_sint(value) : fallback;
}
static float get_float(yyjson_val *obj, const char *key, float fallback) {
    yyjson_val *value = yyjson_obj_get(obj, key);
    return yyjson_is_num(value) ? (float)yyjson_get_num(value) : fallback;
}

BongoCatResult bongo_cat_session_load(const char *path,
    BongoCatSessionState *session, BongoCatError *error) {
    if (!path || !session) return BONGO_CAT_ERROR_ARGUMENT;
    if (!bongo_cat_path_is_file(path)) return BONGO_CAT_OK;
    yyjson_doc *document = bongo_cat_config_read_document(path,
        SESSION_FORMAT, BONGO_CAT_SESSION_SCHEMA, error);
    if (!document) return BONGO_CAT_ERROR_FORMAT;
    BongoCatSessionState loaded = *session;
    yyjson_val *root = yyjson_doc_get_root(document);
    yyjson_val *window = yyjson_obj_get(root, "window");
    if (yyjson_is_obj(window)) {
        loaded.window.visible = get_bool(window, "visible", loaded.window.visible);
        loaded.window.scale_percent = get_float(window, "scalePercent",
            loaded.window.scale_percent);
        loaded.window.opacity_percent = get_float(window, "opacityPercent",
            loaded.window.opacity_percent);
        yyjson_val *position = yyjson_obj_get(window, "position");
        if (yyjson_is_obj(position)) {
            yyjson_val *x = yyjson_obj_get(position, "x");
            yyjson_val *y = yyjson_obj_get(position, "y");
            if (yyjson_is_num(x) && yyjson_is_num(y)) {
                loaded.window.x = (int)yyjson_get_sint(x);
                loaded.window.y = (int)yyjson_get_sint(y);
                loaded.window.position_known = true;
            }
        }
        yyjson_val *size = yyjson_obj_get(window, "size");
        if (yyjson_is_obj(size)) {
            loaded.window.width = get_int(size, "width", loaded.window.width);
            loaded.window.height = get_int(size, "height", loaded.window.height);
        }
    }
    yyjson_val *model = yyjson_obj_get(root, "activeModelId");
    if (yyjson_is_str(model)) snprintf(loaded.active_model_id,
        sizeof(loaded.active_model_id), "%s", yyjson_get_str(model));
    yyjson_doc_free(document);
    bongo_cat_session_validate(&loaded);
    *session = loaded;
    return BONGO_CAT_OK;
}

BongoCatResult bongo_cat_session_save(const char *path,
    const BongoCatSessionState *session, BongoCatError *error) {
    if (!path || !session) return BONGO_CAT_ERROR_ARGUMENT;
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) return BONGO_CAT_ERROR_MEMORY;
    yyjson_mut_val *root = yyjson_mut_obj(doc); yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_strcpy(doc, root, "format", SESSION_FORMAT);
    yyjson_mut_obj_add_int(doc, root, "schemaVersion", BONGO_CAT_SESSION_SCHEMA);
    yyjson_mut_val *window = yyjson_mut_obj_add_obj(doc, root, "window");
    yyjson_mut_obj_add_bool(doc, window, "visible", session->window.visible);
    yyjson_mut_obj_add_real(doc, window, "scalePercent",
        session->window.scale_percent);
    yyjson_mut_obj_add_real(doc, window, "opacityPercent",
        session->window.opacity_percent);
    if (session->window.position_known) {
        yyjson_mut_val *position = yyjson_mut_obj_add_obj(doc, window,
            "position");
        yyjson_mut_obj_add_int(doc, position, "x", session->window.x);
        yyjson_mut_obj_add_int(doc, position, "y", session->window.y);
    }
    yyjson_mut_val *size = yyjson_mut_obj_add_obj(doc, window, "size");
    yyjson_mut_obj_add_int(doc, size, "width", session->window.width);
    yyjson_mut_obj_add_int(doc, size, "height", session->window.height);
    yyjson_mut_obj_add_strcpy(doc, root, "activeModelId",
        session->active_model_id);
    BongoCatResult result = bongo_cat_config_write_document(path, doc,
        "session file", error);
    yyjson_mut_doc_free(doc);
    return result;
}
