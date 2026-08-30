#include "model_import.h"
#include "model_import_tauri_mver_internal.h"
#include "bongo_cat/file.h"
#include "bongo_cat/image.h"
#include "bongo_cat/json.h"
#include "bongo_cat/path.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>
static bool resource_file(const BongoCatImportCandidate *candidate, const char *name,
    char *path, size_t capacity) {
    const char *roots[3] = {candidate->assets, candidate->directory,
        candidate->package_root};
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i) {
        if (!roots[i][0]) continue;
        char resources[BONGO_CAT_PATH_CAP];
        if (bongo_cat_path_join(resources, sizeof(resources), roots[i],
                "resources") && bongo_cat_path_join(path, capacity,
                resources, name) && bongo_cat_path_is_file(path)) return true;
        if (bongo_cat_path_join(path, capacity, roots[i], name) &&
            bongo_cat_path_is_file(path)) return true;
    }
    return false;
}
static bool write_placeholder(const char *path) {
    static const unsigned char png[] = {
        0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,
        0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,
        0x08,0x06,0x00,0x00,0x00,0x1f,0x15,0xc4,0x89,0x00,0x00,0x00,
        0x0d,0x49,0x44,0x41,0x54,0x08,0xd7,0x63,0xf8,0xcf,0xc0,0xf0,
        0x1f,0x00,0x05,0x00,0x01,0xff,0x89,0x99,0x3d,0x1d,0x00,0x00,
        0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82
    };
    FILE *file = bongo_cat_file_open(path, "wb");
    if (!file) return false;
    bool ok = fwrite(png, 1, sizeof(png), file) == sizeof(png);
    if (fclose(file) != 0) ok = false;
    return ok;
}
static bool copy_image_or_placeholder(const char *source, const char *target,
    BongoCatError *error) {
    bool ok = source && bongo_cat_path_is_file(source)
        ? bongo_cat_path_copy_file(source, target) : write_placeholder(target);
    if (!ok && error) bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
        "Cannot create normalized Mver image: %s", target);
    return ok;
}
static bool copy_mver_image(const BongoCatImportCandidate *candidate,
    const char *mode_root, const char *source_name, const char *target_name,
    const char *fallback, BongoCatError *error) {
    char source[BONGO_CAT_PATH_CAP], target[BONGO_CAT_PATH_CAP];
    const char *selected = fallback;
    if (source_name && resource_file(candidate, source_name, source,
            sizeof(source))) selected = source;
    if (!bongo_cat_path_join(target, sizeof(target), mode_root, target_name))
        return false;
    return copy_image_or_placeholder(selected, target, error);
}
static bool copy_mver_runtime_images(
    const BongoCatImportCandidate *candidate, const char *mode_root,
    BongoCatError *error) {
    char background[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(background, sizeof(background), mode_root,
            candidate->mode == BONGO_CAT_MODE_STANDARD ? "mousebg.png" :
                "bg.png")) return false;
    /* Tauri omits these Mver-only layers. Preserve real files when supplied;
       otherwise use transparent PNGs so the portable package still loads. */
    if (candidate->mode == BONGO_CAT_MODE_STANDARD) {
        static const char *const backgrounds[] = {
            "tabletbg.png", "l2dmousebg.png", "l2dtabletbg.png"
        };
        static const char *const layers[] = {
            "arm.png", "up.png", "mouse.png", "mouse_left.png",
            "mouse_right.png", "mouse_side.png", "tablet.png",
            "tablet_left.png", "tablet_right.png"
        };
        for (size_t i = 0; i < sizeof(backgrounds) / sizeof(backgrounds[0]);
                ++i)
            if (!copy_mver_image(candidate, mode_root, backgrounds[i],
                    backgrounds[i], background, error)) return false;
        for (size_t i = 0; i < sizeof(layers) / sizeof(layers[0]); ++i)
            if (!copy_mver_image(candidate, mode_root, layers[i], layers[i],
                    NULL, error)) return false;
        return true;
    }
    static const char *const idle_hands[] = {
        "lefthand/leftup.png", "righthand/rightup.png"
    };
    for (size_t i = 0; i < sizeof(idle_hands) / sizeof(idle_hands[0]); ++i)
        if (!copy_mver_image(candidate, mode_root, idle_hands[i],
                idle_hands[i], NULL, error)) return false;
    if (candidate->mode != BONGO_CAT_MODE_GAMEPAD) return true;
    static const char *const gamepad[] = {
        "arm_L.png", "arm_R.png", "left_stick.png", "left_stick_down.png",
        "right_stick.png", "right_stick_down.png"
    };
    for (size_t i = 0; i < sizeof(gamepad) / sizeof(gamepad[0]); ++i)
        if (!copy_mver_image(candidate, mode_root, gamepad[i], gamepad[i],
                NULL, error)) return false;
    return true;
}
static void mver_window_size(const BongoCatImportCandidate *candidate, int *width,
    int *height) {
    *width = 612;
    *height = 352;
    char source[BONGO_CAT_PATH_CAP];
    if ((resource_file(candidate, "background.png", source,
             sizeof(source)) ||
            resource_file(candidate, "cover.png", source, sizeof(source))) &&
        !bongo_cat_image_info(source, width, height)) {
        *width = 612;
        *height = 352;
    }
}
static bool add_matrix(yyjson_mut_doc *document, yyjson_mut_val *mode,
    const char *name, const TauriKeyFiles *files) {
    yyjson_mut_val *matrix = yyjson_mut_obj_add_arr(document, mode, name);
    if (!matrix) return false;
    for (size_t i = 0; i < files->count; ++i) {
        yyjson_mut_val *row = yyjson_mut_arr_add_arr(document, matrix);
        if (!row || !yyjson_mut_arr_add_int(document, row, files->values[i].code))
            return false;
    }
    return true;
}
static bool config_write(const char *path, BongoCatModelMode mode,
    const TauriKeyFiles *left, const TauriKeyFiles *right, int width, int height, BongoCatError *error) {
    yyjson_mut_doc *document = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = document ? yyjson_mut_obj(document) : NULL;
    if (document) yyjson_mut_doc_set_root(document, root);
    yyjson_mut_val *decoration = root ? yyjson_mut_obj_add_obj(document, root,
        "decoration") : NULL;
    yyjson_mut_val *mode_object = root ? yyjson_mut_obj_add_obj(document, root,
        bongo_cat_mode_name(mode)) : NULL;
    yyjson_mut_val *window_size = decoration ? yyjson_mut_obj_add_arr(document,
        decoration, "window_size") : NULL;
    bool ok = root && decoration && mode_object && window_size &&
        yyjson_mut_arr_add_int(document, window_size, width) &&
        yyjson_mut_arr_add_int(document, window_size, height) &&
        /* Tauri resources are authored in the same pixel canvas as the
           generated Mver input images. Make the projection explicit so a
           missing legacy l2d_correct value cannot apply the 1.1 default. */
        yyjson_mut_obj_add_real(document, decoration, "l2d_correct", 1.0) &&
        yyjson_mut_obj_add_arr(document, decoration, "l2d_offset") &&
        yyjson_mut_obj_add_int(document, root, "mode",
            mode == BONGO_CAT_MODE_STANDARD ? 1 :
            mode == BONGO_CAT_MODE_KEYBOARD ? 2 : 3) &&
        yyjson_mut_obj_add_bool(document, mode_object, "l2d", true);
    if (ok && mode == BONGO_CAT_MODE_STANDARD)
        ok = add_matrix(document, mode_object, "hand", left) &&
            yyjson_mut_obj_add_bool(document, mode_object, "mouse", false) &&
            yyjson_mut_obj_add_arr(document, mode_object, "keyboard");
    if (ok && mode != BONGO_CAT_MODE_STANDARD)
        ok = add_matrix(document, mode_object, "lefthand", left) &&
            add_matrix(document, mode_object, "righthand", right) &&
            yyjson_mut_obj_add_arr(document, mode_object, "keyboard");
    if (ok && mode == BONGO_CAT_MODE_GAMEPAD)
        ok = yyjson_mut_obj_add_int(document, mode_object, "input_mode", 1);
    if (ok) ok = yyjson_mut_obj_add_arr(document, mode_object, "face") &&
        yyjson_mut_obj_add_arr(document, mode_object, "sounds") &&
        yyjson_mut_obj_add_arr(document, mode_object, "l2d_expression") &&
        yyjson_mut_obj_add_arr(document, mode_object, "l2d_motion") &&
        yyjson_mut_obj_add_arr(document, mode_object, "l2d_motion_lockhand");
    if (ok) ok = bongo_cat_json_write_file(path, document, YYJSON_WRITE_PRETTY,
        NULL);
    yyjson_mut_doc_free(document);
    if (!ok && error) bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
        "Cannot write normalized Mver configuration: %s", path);
    return ok;
}
static bool copy_preview(const BongoCatImportCandidate *candidate, const char *mode_root, BongoCatError *error) {
    char source[BONGO_CAT_PATH_CAP], cat[BONGO_CAT_PATH_CAP];
    char target[BONGO_CAT_PATH_CAP];
    bool have_cover = resource_file(candidate, "cover.png", source,
        sizeof(source));
    if (!bongo_cat_path_join(cat, sizeof(cat), mode_root, "cat.png") ||
        !copy_image_or_placeholder(have_cover ? source : NULL, cat, error))
        return false;
    bool have_background = resource_file(candidate, "background.png", source,
        sizeof(source));
    if (!have_background && have_cover) {
        /* A cover is a better fallback than an absent preview. */
        snprintf(source, sizeof(source), "%s", cat);
        have_background = true;
    }
    const char *background_name = candidate->mode == BONGO_CAT_MODE_STANDARD
        ? "mousebg.png" : "bg.png";
    if (!bongo_cat_path_join(target, sizeof(target), mode_root,
            background_name))
        return false;
    return copy_image_or_placeholder(have_background ? source : NULL, target,
        error);
}
static bool copy_keys(const BongoCatImportCandidate *candidate,
    const char *resource_directory, const char *mode_root, const char *name,
    const char *mver_group, TauriKeyFiles *files, BongoCatError *error) {
    char source[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(source, sizeof(source), resource_directory, name))
        return false;
    if (!bongo_cat_path_is_dir(source)) {
        char parent[BONGO_CAT_PATH_CAP];
        if (!bongo_cat_path_join(parent, sizeof(parent), candidate->directory,
                "resources") || !bongo_cat_path_join(source, sizeof(source),
                parent, name) || !bongo_cat_path_is_dir(source)) return true;
    }
    if (!bongo_cat_path_enumerate(source, bongo_cat_tauri_collect_keys, files))
        return false;
    qsort(files->values, files->count, sizeof(files->values[0]),
        bongo_cat_tauri_compare_keys);
    char destination[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(destination, sizeof(destination), mode_root,
            mver_group) || !bongo_cat_path_create_directory(destination))
        return false;
    for (size_t i = 0; i < files->count; ++i) {
        char filename[32], path[BONGO_CAT_PATH_CAP];
        snprintf(filename, sizeof(filename), "%zu.png", i);
        if (!bongo_cat_path_join(path, sizeof(path), destination, filename) ||
            !bongo_cat_path_copy_file(files->values[i].path, path)) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
                "Cannot copy tauri input image: %s", files->values[i].path);
            return false;
        }
    }
    return true;
}
bool bongo_cat_import_tauri_convert_to_mver(const BongoCatImportCandidate *candidate,
    const char *target,
    BongoCatImportCandidate *installed, BongoCatError *error) {
    if (!candidate || !target || !installed ||
        candidate->format != BONGO_CAT_IMPORT_TAURI) return false;
    char img[BONGO_CAT_PATH_CAP], mode_root[BONGO_CAT_PATH_CAP], model_root[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_create_directory(target) ||
        !bongo_cat_path_join(img, sizeof(img), target, "img") ||
        !bongo_cat_path_join(mode_root, sizeof(mode_root), img,
            bongo_cat_mode_name(candidate->mode)) ||
        !bongo_cat_path_join(model_root, sizeof(model_root), mode_root,
            "cat_model") || !bongo_cat_path_create_directory(mode_root) ||
        !bongo_cat_path_create_directory(model_root)) return false;
    if (!bongo_cat_tauri_copy_model_tree(candidate->directory, model_root, 0,
            error))
        return false;
    char resource_root[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(resource_root, sizeof(resource_root),
            candidate->assets, "resources") || !bongo_cat_path_is_dir(resource_root)) {
        if (!bongo_cat_path_join(resource_root, sizeof(resource_root),
                candidate->directory, "resources") ||
            !bongo_cat_path_is_dir(resource_root))
            snprintf(resource_root, sizeof(resource_root), "%s",
                candidate->assets);
    }
    TauriKeyFiles left = {0}, right = {0};
    if (candidate->mode == BONGO_CAT_MODE_STANDARD) {
        if (!copy_keys(candidate, resource_root, mode_root, "left-keys", "hand",
                &left, error)) return false;
    } else if (!copy_keys(candidate, resource_root, mode_root, "left-keys",
            "lefthand", &left, error) ||
        !copy_keys(candidate, resource_root, mode_root, "right-keys", "righthand",
            &right, error)) return false;
    char source[BONGO_CAT_PATH_CAP], fallback[BONGO_CAT_PATH_CAP];
    bool have_fallback = resource_file(candidate, "cover.png", source,
        sizeof(source)) || resource_file(candidate, "background.png", source,
        sizeof(source));
    if (!left.count) {
        char hand[BONGO_CAT_PATH_CAP];
        if (!bongo_cat_path_join(hand, sizeof(hand), mode_root,
                candidate->mode == BONGO_CAT_MODE_STANDARD ? "hand" : "lefthand") ||
            !bongo_cat_path_create_directory(hand) ||
            !bongo_cat_path_join(fallback, sizeof(fallback), hand, "0.png") ||
            !copy_image_or_placeholder(have_fallback ? source : NULL, fallback,
                error)) return false;
        left.values[0].code = candidate->mode == BONGO_CAT_MODE_GAMEPAD ? 0 : 65;
        left.count = 1;
    }
    if (candidate->mode != BONGO_CAT_MODE_STANDARD && !right.count) {
        char hand[BONGO_CAT_PATH_CAP];
        if (!bongo_cat_path_join(hand, sizeof(hand), mode_root, "righthand") ||
            !bongo_cat_path_create_directory(hand) ||
            !bongo_cat_path_join(fallback, sizeof(fallback), hand, "0.png") ||
            !copy_image_or_placeholder(have_fallback ? source : NULL, fallback,
                error)) return false;
        right.values[0].code = candidate->mode == BONGO_CAT_MODE_GAMEPAD ? 1 : 65;
        right.count = 1;
    }
    int width, height;
    mver_window_size(candidate, &width, &height);
    char config[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(config, sizeof(config), target, "config.json") ||
        !config_write(config, candidate->mode, &left, &right, width, height,
            error) ||
        !copy_preview(candidate, mode_root, error) ||
        !copy_mver_runtime_images(candidate, mode_root, error)) return false;
    *installed = *candidate;
    snprintf(installed->directory, sizeof(installed->directory), "%s", model_root);
    snprintf(installed->assets, sizeof(installed->assets), "%s", mode_root);
    snprintf(installed->package_root, sizeof(installed->package_root), "%s", target);
    snprintf(installed->config, sizeof(installed->config), "%s", config);
    installed->overrides[0] = '\0';
    installed->patch_root[0] = '\0';
    installed->format = BONGO_CAT_IMPORT_MVER;
    installed->gamepad_buttons = candidate->mode == BONGO_CAT_MODE_GAMEPAD;
    return true;
}
