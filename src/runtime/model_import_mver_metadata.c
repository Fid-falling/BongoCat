#include "model_import.h"
#include "model_import_mver_internal.h"
#include "bongo_cat_neo/file.h"
#include "bongo_cat_neo/path.h"

#include <stdio.h>
#include <string.h>
#include <yyjson.h>

#define MVER_METADATA ".bongo-cat-neo-mver.json"

static bool append(char *output, size_t capacity, const char *value) {
    size_t used = strlen(output), length = strlen(value);
    if (used + length >= capacity) return false;
    memcpy(output + used, value, length + 1);
    return true;
}

static const char *shortcut_key(int code, char generated[16]) {
    BongoCatNeoMverKeyNames names = bongo_cat_neo_mver_device_names(code, 0, 1);
    const char *name = names.items[0] ? names.items[0] : names.generated;
    if (!name || !*name || bongo_cat_neo_mver_modifier_index(code) >= 0) return NULL;
    if (strncmp(name, "Key", 3) == 0 || strncmp(name, "Num", 3) == 0) {
        generated[0] = name[3]; generated[1] = '\0'; return generated;
    }
    if (strcmp(name, "UpArrow") == 0) return "ArrowUp";
    if (strcmp(name, "DownArrow") == 0) return "ArrowDown";
    if (strcmp(name, "LeftArrow") == 0) return "ArrowLeft";
    if (strcmp(name, "RightArrow") == 0) return "ArrowRight";
    if (strcmp(name, "Return") == 0) return "Enter";
    return name;
}

static bool keyboard_chord(yyjson_val *row, char *output, size_t capacity) {
    output[0] = '\0';
    if (!yyjson_is_arr(row)) return false;
    bool modifiers[3] = {false}, primary = false;
    size_t index, count; yyjson_val *key;
    yyjson_arr_foreach(row, index, count, key) {
        if (!yyjson_is_int(key) && !yyjson_is_uint(key)) continue;
        int code = (int)yyjson_get_int(key), modifier = bongo_cat_neo_mver_modifier_index(code);
        if (modifier >= 0) { modifiers[modifier] = true; continue; }
        char generated[16]; const char *name = shortcut_key(code, generated);
        if (!name || primary) return false;
        primary = append(output, capacity, name);
    }
    if (!primary) return false;
    char key_name[BONGO_CAT_NEO_SHORTCUT_CAP];
    snprintf(key_name, sizeof(key_name), "%s", output); output[0] = '\0';
    return (!modifiers[1] || append(output, capacity, "Control+")) &&
        (!modifiers[0] || append(output, capacity, "Shift+")) &&
        (!modifiers[2] || append(output, capacity, "Alt+")) &&
        append(output, capacity, key_name);
}

static bool gamepad_chord(yyjson_val *row, char *output, size_t capacity) {
    if (!yyjson_is_arr(row) || yyjson_arr_size(row) != 1) return false;
    yyjson_val *key = yyjson_arr_get_first(row);
    if (!yyjson_is_int(key) && !yyjson_is_uint(key)) return false;
    BongoCatNeoMverKeyNames names = bongo_cat_neo_mver_gamepad_names(
        (int)yyjson_get_int(key));
    if (!names.count || !names.items[0]) return false;
    return snprintf(output, capacity, "Gamepad:%s", names.items[0]) > 0;
}

static bool chord(const BongoCatNeoImportCandidate *candidate, yyjson_val *row,
    char *output, size_t capacity) {
    if (candidate->mode != BONGO_CAT_NEO_MODE_GAMEPAD)
        return keyboard_chord(row, output, capacity);
    yyjson_val *only = yyjson_is_arr(row) && yyjson_arr_size(row) == 1
        ? yyjson_arr_get_first(row) : NULL;
    int code = (yyjson_is_int(only) || yyjson_is_uint(only))
        ? (int)yyjson_get_int(only) : -1;
    return code >= 0 && code <= 15 ? gamepad_chord(row, output, capacity)
        : keyboard_chord(row, output, capacity);
}

static bool sound_source(const BongoCatNeoImportCandidate *candidate, size_t index,
    char *source, size_t capacity, char *relative, size_t relative_capacity) {
    static const char *extensions[] = {"wav", "ogg", "flac"};
    for (size_t i = 0; i < sizeof(extensions) / sizeof(extensions[0]); ++i) {
        char name[40], sounds[BONGO_CAT_NEO_PATH_CAP];
        snprintf(name, sizeof(name), "%zu.%s", index, extensions[i]);
        if (!bongo_cat_neo_path_join(sounds, sizeof(sounds), candidate->assets, "sounds") ||
            !bongo_cat_neo_path_join(source, capacity, sounds, name) ||
            !bongo_cat_neo_path_is_file(source)) continue;
        snprintf(relative, relative_capacity, "resources/sounds/%s", name);
        return true;
    }
    return false;
}

static bool add_rows(yyjson_mut_doc *output, yyjson_mut_val *items, yyjson_val *rows,
    const BongoCatNeoImportCandidate *candidate, const char *kind,
    const char *group, bool lock_motion) {
    if (!yyjson_is_arr(rows)) return true;
    size_t index, count; yyjson_val *row;
    yyjson_arr_foreach(rows, index, count, row) {
        char shortcut[BONGO_CAT_NEO_SHORTCUT_CAP];
        if (!chord(candidate, row, shortcut, sizeof(shortcut))) continue;
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(output, items);
        if (!item || !yyjson_mut_obj_add_strcpy(output, item, "kind", kind) ||
            !yyjson_mut_obj_add_int(output, item, "index", (int)index) ||
            !yyjson_mut_obj_add_strcpy(output, item, "shortcut", shortcut)) return false;
        if (group && !yyjson_mut_obj_add_strcpy(output, item, "group", group)) return false;
        if (lock_motion && !yyjson_mut_obj_add_bool(output, item, "lock", true)) return false;
    }
    return true;
}

static bool add_sounds(yyjson_mut_doc *output, yyjson_mut_val *items, yyjson_val *rows,
    const BongoCatNeoImportCandidate *candidate, const char *target) {
    if (!yyjson_is_arr(rows)) return true;
    char target_resources[BONGO_CAT_NEO_PATH_CAP], target_sounds[BONGO_CAT_NEO_PATH_CAP];
    if (!bongo_cat_neo_path_join(target_resources, sizeof(target_resources), target, "resources") ||
        !bongo_cat_neo_path_join(target_sounds, sizeof(target_sounds), target_resources, "sounds") ||
        !SDL_CreateDirectory(target_sounds)) return false;
    size_t index, count; yyjson_val *row;
    yyjson_arr_foreach(rows, index, count, row) {
        char shortcut[BONGO_CAT_NEO_SHORTCUT_CAP], source[BONGO_CAT_NEO_PATH_CAP];
        char relative[BONGO_CAT_NEO_PATH_CAP], destination[BONGO_CAT_NEO_PATH_CAP];
        if (!chord(candidate, row, shortcut, sizeof(shortcut)) ||
            !sound_source(candidate, index, source, sizeof(source), relative, sizeof(relative))) continue;
        const char *name = bongo_cat_neo_path_name(source);
        if (!bongo_cat_neo_path_join(destination, sizeof(destination), target_sounds, name) ||
            !SDL_CopyFile(source, destination)) return false;
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(output, items);
        if (!item || !yyjson_mut_obj_add_str(output, item, "kind", "sound") ||
            !yyjson_mut_obj_add_strcpy(output, item, "shortcut", shortcut) ||
            !yyjson_mut_obj_add_strcpy(output, item, "sound", relative)) return false;
    }
    return true;
}

static void motion_group(const BongoCatNeoImportCandidate *candidate, bool lock,
    char *group, size_t capacity) {
    char path[BONGO_CAT_NEO_PATH_CAP];
    if (!bongo_cat_neo_path_join(path, sizeof(path), candidate->directory,
        candidate->setting)) return;
    yyjson_doc *document = yyjson_read_file(path, 0, NULL, NULL);
    yyjson_val *refs = document ? yyjson_obj_get(yyjson_doc_get_root(document),
        "FileReferences") : NULL;
    yyjson_val *motions = yyjson_obj_get(refs, "Motions");
    const char *fallback = "";
    size_t index, count; yyjson_val *key, *value;
    yyjson_obj_foreach(motions, index, count, key, value) {
        const char *name = yyjson_get_str(key);
        if (!name || strcmp(name, "Idle") == 0) continue;
        bool is_lock = strstr(name, "lock") != NULL || strstr(name, "Lock") != NULL;
        if (!fallback[0]) fallback = name;
        if (is_lock == lock) { snprintf(group, capacity, "%s", name); break; }
    }
    if (!group[0]) snprintf(group, capacity, "%s", fallback);
    yyjson_doc_free(document);
}

bool bongo_cat_neo_import_mver_metadata(const BongoCatNeoImportCandidate *candidate,
    const char *target, BongoCatNeoError *error) {
    if (candidate->format != BONGO_CAT_NEO_IMPORT_MVER) return true;
    yyjson_doc *source = yyjson_read_file(candidate->config,
        YYJSON_READ_JSON5 | YYJSON_READ_ALLOW_INVALID_UNICODE, NULL, NULL);
    yyjson_val *mode = source ? yyjson_obj_get(yyjson_doc_get_root(source),
        bongo_cat_neo_mode_name(candidate->mode)) : NULL;
    yyjson_mut_doc *output = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = output ? yyjson_mut_obj(output) : NULL;
    yyjson_mut_val *items = root ? yyjson_mut_obj_add_arr(output, root, "bindings") : NULL;
    if (output) yyjson_mut_doc_set_root(output, root);
    char motion[BONGO_CAT_NEO_ID_CAP] = {0}, lock_motion[BONGO_CAT_NEO_ID_CAP] = {0};
    motion_group(candidate, false, motion, sizeof(motion));
    motion_group(candidate, true, lock_motion, sizeof(lock_motion));
    bool ok = yyjson_is_obj(mode) && items &&
        add_rows(output, items, yyjson_obj_get(mode, "l2d_expression"), candidate,
            "expression", NULL, false) &&
        add_rows(output, items, yyjson_obj_get(mode, "l2d_motion"), candidate,
            "motion", motion, false) &&
        add_rows(output, items, yyjson_obj_get(mode, "l2d_motion_lockhand"), candidate,
            "motion", lock_motion, true) &&
        add_sounds(output, items, yyjson_obj_get(mode, "sounds"), candidate, target);
    char path[BONGO_CAT_NEO_PATH_CAP];
    if (ok) ok = bongo_cat_neo_path_join(path, sizeof(path), target, MVER_METADATA) &&
        yyjson_mut_write_file(path, output, 0, NULL, NULL);
    yyjson_mut_doc_free(output); yyjson_doc_free(source);
    if (!ok) bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_FORMAT,
        "Cannot convert Mver behavior metadata: %s", candidate->config);
    return ok;
}

void bongo_cat_neo_import_apply_metadata(BongoCatNeoApp *app, const char *model_id,
    const char *directory) {
    char path[BONGO_CAT_NEO_PATH_CAP];
    if (!bongo_cat_neo_path_join(path, sizeof(path), directory, MVER_METADATA)) return;
    yyjson_doc *document = yyjson_read_file(path, 0, NULL, NULL);
    yyjson_val *bindings = document ? yyjson_obj_get(yyjson_doc_get_root(document), "bindings") : NULL;
    size_t index, count; yyjson_val *item;
    yyjson_arr_foreach(bindings, index, count, item) {
        const char *kind = yyjson_get_str(yyjson_obj_get(item, "kind"));
        if (!kind) continue;
        const char *shortcut = yyjson_get_str(yyjson_obj_get(item, "shortcut"));
        const char *group = yyjson_get_str(yyjson_obj_get(item, "group"));
        int behavior_index = strcmp(kind, "sound") == 0 ? 0 :
            (int)yyjson_get_int(yyjson_obj_get(item, "index"));
        if (strcmp(kind, "sound") == 0) {
            for (size_t before = 0; before < index; ++before) {
                yyjson_val *previous = yyjson_arr_get(bindings, before);
                const char *previous_kind = yyjson_get_str(yyjson_obj_get(previous, "kind"));
                if (previous_kind && strcmp(previous_kind, "sound") == 0) behavior_index++;
            }
        }
        char id[BONGO_CAT_NEO_PATH_CAP];
        if (strcmp(kind, "sound") == 0)
            snprintf(id, sizeof(id), "%s:sound:%d", model_id, behavior_index);
        else if (strcmp(kind, "expression") == 0)
            snprintf(id, sizeof(id), "%s:expression:%d", model_id, behavior_index);
        else snprintf(id, sizeof(id), "%s:motion:%s:%d",
            model_id, group ? group : "", behavior_index);
        bool present = false;
        for (size_t existing = 0; existing < app->config.behavior_shortcut_count; ++existing)
            if (strcmp(app->config.behavior_shortcuts[existing].id, id) == 0) {
                present = true; break;
            }
        if (present) continue;
        if (app->config.behavior_shortcut_count >= BONGO_CAT_NEO_BEHAVIOR_CAP) break;
        BongoCatNeoBehaviorShortcut *value =
            &app->config.behavior_shortcuts[app->config.behavior_shortcut_count++];
        snprintf(value->id, sizeof(value->id), "%s", id);
        snprintf(value->shortcut, sizeof(value->shortcut), "%s", shortcut ? shortcut : "");
    }
    yyjson_doc_free(document);
}
