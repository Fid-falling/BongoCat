#include "model_import.h"
#include "model_import_mver_internal.h"
#include "bongo_cat_neo/file.h"
#include "bongo_cat_neo/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

typedef BongoCatNeoMverKeyNames KeyNames;

static bool child_is_dir(const char *root, const char *name) {
    char path[BONGO_CAT_NEO_PATH_CAP];
    return bongo_cat_neo_path_join(path, sizeof(path), root, name) && bongo_cat_neo_path_is_dir(path);
}

int bongo_cat_neo_mver_modifier_index(int code) {
    return code == 16 ? 0 : code == 17 ? 1 : code == 18 ? 2 : -1;
}

static void count_modifiers(yyjson_val *matrix, size_t counts[3]) {
    size_t row_index, row_count; yyjson_val *row;
    yyjson_arr_foreach(matrix, row_index, row_count, row) {
        size_t key_index, key_count; yyjson_val *key;
        yyjson_arr_foreach(row, key_index, key_count, key) {
            int index = (yyjson_is_int(key) || yyjson_is_uint(key))
                ? bongo_cat_neo_mver_modifier_index((int)yyjson_get_int(key)) : -1;
            if (index >= 0) counts[index]++;
        }
    }
}

KeyNames bongo_cat_neo_mver_device_names(int code, size_t occurrence, size_t total) {
    KeyNames names = {0};
    if (code >= 48 && code <= 57) {
        snprintf(names.generated, sizeof(names.generated), "Num%c", (char)code);
    } else if (code >= 65 && code <= 90) {
        snprintf(names.generated, sizeof(names.generated), "Key%c", (char)code);
    } else if (code >= 112 && code <= 123) {
        snprintf(names.generated, sizeof(names.generated), "F%d", code - 111);
    } else if (bongo_cat_neo_mver_modifier_index(code) >= 0) {
        static const char *const modifiers[][2] = {
            {"ShiftLeft", "ShiftRight"}, {"ControlLeft", "ControlRight"},
            {"Alt", "AltGr"}
        };
        int index = bongo_cat_neo_mver_modifier_index(code);
        if (total > 1) {
            names.items[0] = modifiers[index][occurrence ? 1 : 0];
            names.count = 1;
        } else {
            names.items[0] = modifiers[index][0];
            names.items[1] = modifiers[index][1];
            names.count = 2;
        }
        return names;
    } else {
        static const struct { int code; const char *name; } map[] = {
            {8,"Backspace"},{9,"Tab"},{13,"Return"},{19,"Pause"},{20,"CapsLock"},
            {27,"Escape"},{32,"Space"},{33,"PageUp"},{34,"PageDown"},{35,"End"},
            {36,"Home"},{37,"LeftArrow"},{38,"UpArrow"},{39,"RightArrow"},
            {40,"DownArrow"},{44,"PrintScreen"},{45,"Insert"},{46,"Delete"},
            {91,"Meta"},{92,"Meta"},{93,"Apps"},{96,"Kp0"},{97,"Kp1"},
            {98,"Kp2"},{99,"Kp3"},{100,"Kp4"},{101,"Kp5"},{102,"Kp6"},
            {103,"Kp7"},{104,"Kp8"},{105,"Kp9"},{106,"KpMultiply"},
            {107,"KpPlus"},{109,"KpMinus"},{110,"KpDecimal"},{111,"KpDivide"},
            {144,"NumLock"},{145,"ScrollLock"},{186,"Semicolon"},{187,"Equal"},
            {188,"Comma"},{189,"Minus"},{190,"Period"},{191,"Slash"},
            {192,"BackQuote"},{219,"BracketLeft"},{220,"Backslash"},
            {221,"BracketRight"},{222,"Quote"}
        };
        for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); ++i)
            if (map[i].code == code) { names.items[0] = map[i].name; names.count = 1; break; }
        return names;
    }
    names.count = 1;
    return names;
}

KeyNames bongo_cat_neo_mver_gamepad_names(int code) {
    static const char *map[] = {"South", "East", "West", "North", "LeftTrigger",
        "RightTrigger", "LeftTrigger2", "RightTrigger2", "LeftThumb", "RightThumb",
        "DPadLeft", "DPadRight", "DPadUp", "DPadDown", "Start", "Select"};
    KeyNames names = {0};
    if (code >= 0 && (size_t)code < sizeof(map) / sizeof(map[0])) {
        names.items[0] = map[code]; names.count = 1;
    }
    return names;
}

static bool keyboard_index(yyjson_val *matrix, int code, size_t *result) {
    if (!yyjson_is_arr(matrix)) return false;
    size_t row_index, row_count; yyjson_val *row;
    yyjson_arr_foreach(matrix, row_index, row_count, row) {
        size_t key_index, key_count; yyjson_val *key;
        yyjson_arr_foreach(row, key_index, key_count, key) {
            if ((yyjson_is_int(key) || yyjson_is_uint(key)) &&
                yyjson_get_int(key) == code) {
                *result = row_index;
                return true;
            }
        }
    }
    return false;
}

static bool process_matrix(const BongoCatNeoImportCandidate *candidate, yyjson_val *matrix,
    yyjson_val *before, yyjson_val *after, yyjson_val *keyboard_matrix,
    const char *hand_name, const char *key_group, const char *target,
    BongoCatNeoError *error) {
    if (!yyjson_is_arr(matrix)) return false;
    char hand_dir[BONGO_CAT_NEO_PATH_CAP], keyboard_dir[BONGO_CAT_NEO_PATH_CAP];
    char resources[BONGO_CAT_NEO_PATH_CAP], output_dir[BONGO_CAT_NEO_PATH_CAP];
    if (!bongo_cat_neo_path_join(hand_dir, sizeof(hand_dir), candidate->assets, hand_name) ||
        !bongo_cat_neo_path_join(keyboard_dir, sizeof(keyboard_dir), candidate->assets, "keyboard") ||
        !bongo_cat_neo_path_join(resources, sizeof(resources), target, "resources") ||
        !bongo_cat_neo_path_join(output_dir, sizeof(output_dir), resources, key_group) ||
        !SDL_CreateDirectory(output_dir)) return false;
    size_t modifier_total[3] = {0}, modifier_seen[3] = {0};
    count_modifiers(before, modifier_seen);
    memcpy(modifier_total, modifier_seen, sizeof(modifier_total));
    count_modifiers(matrix, modifier_total);
    count_modifiers(after, modifier_total);
    size_t index, count; yyjson_val *keys;
    yyjson_arr_foreach(matrix, index, count, keys) {
        char hand[BONGO_CAT_NEO_PATH_CAP], filename[32];
        snprintf(filename, sizeof(filename), "%zu.png", index);
        if (!bongo_cat_neo_path_join(hand, sizeof(hand), hand_dir, filename) ||
            !bongo_cat_neo_path_is_file(hand)) {
            size_t missing_index, missing_count; yyjson_val *missing;
            yyjson_arr_foreach(keys, missing_index, missing_count, missing) {
                int modifier = (yyjson_is_int(missing) || yyjson_is_uint(missing))
                    ? bongo_cat_neo_mver_modifier_index((int)yyjson_get_int(missing)) : -1;
                if (modifier >= 0) modifier_seen[modifier]++;
            }
            continue;
        }
        size_t key_index, key_count; yyjson_val *key;
        yyjson_arr_foreach(keys, key_index, key_count, key) {
            if (!yyjson_is_int(key) && !yyjson_is_uint(key)) continue;
            int code = (int)yyjson_get_int(key);
            char keyboard[BONGO_CAT_NEO_PATH_CAP];
            size_t keyboard_row;
            const char *keyboard_path = NULL;
            if (keyboard_index(keyboard_matrix, code, &keyboard_row)) {
                snprintf(filename, sizeof(filename), "%zu.png", keyboard_row);
                if (bongo_cat_neo_path_join(keyboard, sizeof(keyboard), keyboard_dir, filename) &&
                    bongo_cat_neo_path_is_file(keyboard)) keyboard_path = keyboard;
            }
            int modifier = bongo_cat_neo_mver_modifier_index(code);
            size_t occurrence = modifier >= 0 ? modifier_seen[modifier]++ : 0;
            KeyNames names = candidate->mode == BONGO_CAT_NEO_MODE_GAMEPAD
                ? bongo_cat_neo_mver_gamepad_names(code) : bongo_cat_neo_mver_device_names(code, occurrence,
                    modifier >= 0 ? modifier_total[modifier] : 0);
            if (!bongo_cat_neo_mver_emit_pair(hand, keyboard_path, output_dir, names, error)) return false;
        }
    }
    return true;
}

bool bongo_cat_neo_import_mver_assets(const BongoCatNeoImportCandidate *candidate,
    const char *target, BongoCatNeoError *error) {
    if (candidate->format != BONGO_CAT_NEO_IMPORT_MVER) return true;
    const char *left = candidate->mode == BONGO_CAT_NEO_MODE_STANDARD ? "hand" : "lefthand";
    if (!child_is_dir(candidate->assets, left)) return false;
    FILE *file = bongo_cat_neo_file_open(candidate->config, "rb");
    yyjson_doc *document = file ? yyjson_read_fp(file,
        YYJSON_READ_JSON5 | YYJSON_READ_ALLOW_INVALID_UNICODE, NULL, NULL) : NULL;
    if (file) fclose(file);
    yyjson_val *root = document ? yyjson_doc_get_root(document) : NULL;
    yyjson_val *mode = yyjson_is_obj(root)
        ? yyjson_obj_get(root, bongo_cat_neo_mode_name(candidate->mode)) : NULL;
    if (!yyjson_is_obj(mode)) {
        yyjson_doc_free(document);
        bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_FORMAT,
            "Cannot parse Mver input configuration: %s", candidate->config);
        return false;
    }
    bool ok = false;
    yyjson_val *keyboard = yyjson_obj_get(mode, "keyboard");
    if (candidate->mode == BONGO_CAT_NEO_MODE_STANDARD) {
        yyjson_val *hand = yyjson_obj_get(mode, "hand");
        if (yyjson_is_arr(hand)) ok = process_matrix(candidate, hand, NULL, NULL,
            keyboard, "hand", "left-keys", target, error);
    } else {
        yyjson_val *left_keys = yyjson_obj_get(mode, "lefthand");
        yyjson_val *right_keys = yyjson_obj_get(mode, "righthand");
        if (yyjson_is_arr(left_keys) && yyjson_is_arr(right_keys))
            ok = process_matrix(candidate, left_keys, NULL, right_keys, keyboard,
                "lefthand", "left-keys", target, error) &&
            process_matrix(candidate, right_keys, left_keys, NULL, keyboard,
                "righthand", "right-keys", target, error);
    }
    yyjson_doc_free(document);
    if (!ok && error && !error->message[0]) bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_FORMAT,
        "Cannot convert Mver input configuration: %s", candidate->config);
    return ok;
}
