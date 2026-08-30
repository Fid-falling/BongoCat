#include "model_import_tauri_internal.h"

#include "bongo_cat/image.h"
#include "bongo_cat/json.h"

#include <math.h>
#include <string.h>
#include <yyjson.h>

static bool finite_number(yyjson_val *value) {
    return yyjson_is_num(value) && isfinite(yyjson_get_num(value));
}

static bool valid_canvas_size(int width, int height) {
    return width >= 64 && width <= 8192 && height >= 64 && height <= 8192;
}

static bool read_offset(yyjson_val *value, double *x, double *y) {
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) < 2 ||
        !finite_number(yyjson_arr_get(value, 0)) ||
        !finite_number(yyjson_arr_get(value, 1))) return false;
    double next_x = yyjson_get_num(yyjson_arr_get(value, 0));
    double next_y = yyjson_get_num(yyjson_arr_get(value, 1));
    if (next_x < -100.0 || next_x > 100.0 || next_y < -100.0 ||
        next_y > 100.0) return false;
    *x = next_x;
    *y = next_y;
    return true;
}

static bool read_window(yyjson_val *value, int *width, int *height) {
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) < 2 ||
        !yyjson_is_num(yyjson_arr_get(value, 0)) ||
        !yyjson_is_num(yyjson_arr_get(value, 1))) return false;
    double next_width = yyjson_get_num(yyjson_arr_get(value, 0));
    double next_height = yyjson_get_num(yyjson_arr_get(value, 1));
    if (!isfinite(next_width) || !isfinite(next_height) ||
        next_width < 64.0 || next_width > 8192.0 ||
        next_height < 64.0 || next_height > 8192.0)
        return false;
    int integer_width = (int)next_width;
    int integer_height = (int)next_height;
    if ((double)integer_width != next_width ||
        (double)integer_height != next_height) return false;
    *width = integer_width;
    *height = integer_height;
    return true;
}

static bool legacy_converter_canvas(
    const BongoCatImportCandidate *candidate, int width, int height) {
    if (!candidate || !valid_canvas_size(width, height) ||
        width < 256 || height < 256) return false;
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_tauri_find_resource_directory(candidate, "left-keys",
            path, sizeof(path))) return false;
    if (candidate->mode == BONGO_CAT_MODE_STANDARD && width == height &&
        width >= 512)
        return true;
    if (!bongo_cat_tauri_find_resource_file(candidate, "cover.png", path,
            sizeof(path))) return false;
    int cover_width = 0, cover_height = 0;
    return bongo_cat_image_info(path, &cover_width, &cover_height) &&
        valid_canvas_size(cover_width, cover_height) &&
        (cover_width != width || cover_height != height);
}

static void legacy_defaults(const BongoCatImportCandidate *candidate,
    TauriMverCalibration *calibration) {
    calibration->l2d_correct = 1.1;
    calibration->l2d_offset_x = 0.0;
    calibration->l2d_offset_y = 0.0;
    calibration->window_width = 612;
    calibration->window_height = 352;
    calibration->mirror = false;
    char path[BONGO_CAT_PATH_CAP];
    const char *names[] = {"background.png", "cover.png"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (!bongo_cat_tauri_find_resource_file(candidate, names[i], path,
                sizeof(path))) continue;
        int width = 0, height = 0;
        if (bongo_cat_image_info(path, &width, &height) &&
            valid_canvas_size(width, height)) {
            calibration->window_width = width;
            calibration->window_height = height;
            break;
        }
    }
    /* Converter packages before schemaVersion 1 omitted their Mver projection
       calibration. Their model scale is approximately 2.0 across modes; the
       old 1.1 fallback leaves the Live2D layer much smaller than its authored
       background and makes the keyboard or desk appear duplicated. */
    if (legacy_converter_canvas(candidate, calibration->window_width,
            calibration->window_height)) {
        calibration->l2d_correct = 2.0;
        if (candidate->mode == BONGO_CAT_MODE_STANDARD &&
            calibration->window_width == calibration->window_height)
            calibration->l2d_offset_y = -0.005;
    }
}

bool bongo_cat_tauri_read_calibration(
    const BongoCatImportCandidate *candidate,
    TauriMverCalibration *calibration) {
    if (!candidate || !calibration) return false;
    legacy_defaults(candidate, calibration);
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_tauri_find_package_file(candidate,
            BONGO_CAT_TAURI_SOURCE_FILE, path, sizeof(path))) return true;
    yyjson_doc *document = bongo_cat_json_read_file(path, 0, NULL);
    yyjson_val *root = document ? yyjson_doc_get_root(document) : NULL;
    yyjson_val *schema = yyjson_obj_get(root, "schemaVersion");
    const char *kind = yyjson_get_str(yyjson_obj_get(root, "kind"));
    const char *mode = yyjson_get_str(yyjson_obj_get(root, "mode"));
    yyjson_val *decoration = yyjson_obj_get(root, "decoration");
    bool valid = yyjson_is_int(schema) && yyjson_get_int(schema) == 1 &&
        kind && strcmp(kind, "bongo-cat-mver-source") == 0 &&
        (!mode || strcmp(mode, bongo_cat_mode_name(candidate->mode)) == 0) &&
        yyjson_is_obj(decoration);
    if (valid) {
        yyjson_val *scale = yyjson_obj_get(decoration, "l2d_correct");
        if (finite_number(scale)) {
            double value = yyjson_get_num(scale);
            if (value > 0.01 && value <= 100.0)
                calibration->l2d_correct = value;
        }
        read_offset(yyjson_obj_get(decoration, "l2d_offset"),
            &calibration->l2d_offset_x, &calibration->l2d_offset_y);
        read_window(yyjson_obj_get(decoration, "window_size"),
            &calibration->window_width, &calibration->window_height);
        yyjson_val *mirror = yyjson_obj_get(decoration,
            "l2d_horizontal_flip");
        if (yyjson_is_bool(mirror)) calibration->mirror = yyjson_get_bool(mirror);
    }
    yyjson_doc_free(document);
    return true;
}
