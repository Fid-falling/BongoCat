#include "model_import.h"
#include "model_import_mver_internal.h"
#include "runtime.h"
#include "bongo_cat/file.h"
#include "bongo_cat/json.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <string.h>
#include <yyjson.h>

#define MVER_METADATA ".bongo-cat-mver.json"

static bool sound_source(const BongoCatImportCandidate *candidate, size_t index,
    char *source, size_t capacity, char *relative, size_t relative_capacity) {
    static const char *extensions[] = {"wav", "ogg", "flac"};
    for (size_t i = 0; i < sizeof(extensions) / sizeof(extensions[0]); ++i) {
        char name[40], sounds[BONGO_CAT_PATH_CAP];
        snprintf(name, sizeof(name), "%zu.%s", index, extensions[i]);
        if (!bongo_cat_path_join(sounds, sizeof(sounds), candidate->assets, "sounds") ||
            !bongo_cat_path_join(source, capacity, sounds, name) ||
            !bongo_cat_path_is_file(source)) continue;
        snprintf(relative, relative_capacity, "resources/sounds/%s", name);
        return true;
    }
    return false;
}

static bool add_sound_clear(yyjson_mut_doc *output, yyjson_mut_val *items,
    yyjson_val *config, const BongoCatImportCandidate *candidate) {
    yyjson_val *decoration = yyjson_obj_get(config, "decoration");
    yyjson_val *row = yyjson_obj_get(decoration, "soundClear");
    if (!row) return true;
    char shortcut[BONGO_CAT_SHORTCUT_CAP];
    if (!bongo_cat_mver_chord(candidate, row, shortcut, sizeof(shortcut))) return false;
    yyjson_mut_val *item = yyjson_mut_arr_add_obj(output, items);
    return item && yyjson_mut_obj_add_str(output, item, "kind", "sound-clear") &&
        yyjson_mut_obj_add_strcpy(output, item, "shortcut", shortcut);
}

static bool add_sounds(yyjson_mut_doc *output, yyjson_mut_val *items,
    yyjson_val *config, yyjson_val *rows,
    const BongoCatImportCandidate *candidate, const char *target) {
    if (!rows) return true;
    if (!yyjson_is_arr(rows)) return false;
    char target_resources[BONGO_CAT_PATH_CAP], target_sounds[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(target_resources, sizeof(target_resources), target, "resources") ||
        !bongo_cat_path_join(target_sounds, sizeof(target_sounds), target_resources, "sounds") ||
        !bongo_cat_path_create_directory(target_sounds)) return false;
    yyjson_val *decoration = yyjson_obj_get(config, "decoration");
    yyjson_val *keep_value = yyjson_obj_get(decoration, "soundKeep");
    bool keep = !keep_value || yyjson_get_bool(keep_value);
    size_t emitted = 0, index, count; yyjson_val *row;
    yyjson_arr_foreach(rows, index, count, row) {
        char shortcut[BONGO_CAT_SHORTCUT_CAP], source[BONGO_CAT_PATH_CAP];
        char relative[BONGO_CAT_PATH_CAP], destination[BONGO_CAT_PATH_CAP];
        if (!bongo_cat_mver_chord(candidate, row, shortcut, sizeof(shortcut))) return false;
        if (!sound_source(candidate, index, source, sizeof(source), relative,
            sizeof(relative))) continue;
        const char *name = bongo_cat_path_name(source);
        if (!bongo_cat_path_join(destination, sizeof(destination), target_sounds, name) ||
            !bongo_cat_path_copy_file(source, destination)) return false;
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(output, items);
        if (!item || !yyjson_mut_obj_add_str(output, item, "kind", "sound") ||
            !yyjson_mut_obj_add_strcpy(output, item, "shortcut", shortcut) ||
            !yyjson_mut_obj_add_strcpy(output, item, "sound", relative) ||
            (!keep && !yyjson_mut_obj_add_bool(output, item, "momentary", true))) return false;
        emitted++;
    }
    return !emitted || !keep || add_sound_clear(output, items, config, candidate);
}

bool bongo_cat_import_mver_metadata(const BongoCatImportCandidate *candidate,
    const char *target, BongoCatError *error) {
    if (candidate->format != BONGO_CAT_IMPORT_MVER &&
        candidate->format != BONGO_CAT_IMPORT_MVER_PATCH) return true;
    yyjson_doc *source = bongo_cat_json_read_file(candidate->config,
        YYJSON_READ_JSON5 | YYJSON_READ_ALLOW_INVALID_UNICODE, NULL);
    yyjson_val *config = source ? yyjson_doc_get_root(source) : NULL;
    yyjson_val *mode = yyjson_obj_get(config, bongo_cat_mode_name(candidate->mode));
    yyjson_mut_doc *output = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = output ? yyjson_mut_obj(output) : NULL;
    yyjson_mut_val *items = root ? yyjson_mut_obj_add_arr(output, root, "bindings") : NULL;
    if (output) yyjson_mut_doc_set_root(output, root);
    bool ok = yyjson_is_obj(mode) && items &&
        yyjson_mut_obj_add_int(output, root, "version", 1) &&
        bongo_cat_mver_add_behaviors(output, items, mode, candidate, error) &&
        add_sounds(output, items, config, yyjson_obj_get(mode, "sounds"), candidate, target) &&
        bongo_cat_mver_effects(output, items, config, mode, candidate, target);
    char path[BONGO_CAT_PATH_CAP];
    if (ok) ok = bongo_cat_path_join(path, sizeof(path), target, MVER_METADATA) &&
        bongo_cat_json_write_file(path, output, YYJSON_WRITE_PRETTY, NULL);
    yyjson_mut_doc_free(output);
    yyjson_doc_free(source);
    if (!ok && error && !error->message[0])
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Cannot convert Mver behavior metadata: %s", candidate->config);
    return ok;
}
