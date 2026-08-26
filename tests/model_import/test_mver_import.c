#include "model_import_mver_internal.h"
#include "model_storage.h"
#include "preferences_model_glyphs.h"
#include "preferences_state.h"
#include "ui_font_atlas.h"
#include "runtime.h"
#include "test_mver_support.h"
#include "test_mver_import_internal.h"
#include "bongo_cat/image.h"
#include "bongo_cat/json.h"
#include "bongo_cat/path.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>
int failures;
int test_preferences_text(void); int test_mver_nearby_identity(void);
int test_mver_nearby_refresh(void);
int test_mver_missing_motion_groups(void);
int test_model_import_identity(void);
int test_slim_package(void);
static bool chord(const char *json, bool gamepad, const char *expected) {
    yyjson_doc *document = yyjson_read(json, strlen(json), 0);
    BongoCatImportCandidate candidate = {0};
    candidate.gamepad_buttons = gamepad;
    char output[BONGO_CAT_SHORTCUT_CAP];
    bool ok = document && bongo_cat_mver_chord(&candidate,
        yyjson_doc_get_root(document), output, sizeof(output));
    bool matches = ok && expected && strcmp(output, expected) == 0;
    yyjson_doc_free(document);
    return expected ? matches : !ok;
}

static void labels_from_shortcut_rows(void) {
    static const char default_expression[] =
        "\xE9\xBB\x98\xE8\xAE\xA4\xE8\xA1\xA8\xE6\x83\x85";
    static const char keyboard_hidden[] =
        "\xE9\x94\xAE\xE7\x9B\x98\xE6\xB6\x88\xE5\xA4\xB1";
    char *temporary = SDL_GetCurrentDirectory();
    CHECK(temporary != NULL);
    if (!temporary) return;
    char root[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
    snprintf(root, sizeof(root), "%s/bongo-cat-labels-%llu", temporary,
        (unsigned long long)SDL_GetTicksNS());
    CHECK(SDL_CreateDirectory(root));
    CHECK(child(path, sizeof(path), root, "config.json", false));
    CHECK(write_text(path,
        "{/* misleading: \\\"standard\\\" */standard:{"
        "l2d_expression:[[18,//\xE9\xBB\x98\xE8\xAE\xA4\xE8\xA1\xA8\xE6\x83\x85\n76],"
        "[18,77]],l2d_motion:[],l2d_motion_lockhand:"
        "[[18,//\xE9\x94\xAE\xE7\x9B\x98\xE6\xB6\x88\xE5\xA4\xB1\n49]],sounds:[]},"
        "keyboard:{l2d_expression:[]}}"));
    BongoCatMverLabels labels = {0};
    CHECK(bongo_cat_mver_labels_load(path, "standard", &labels));
    CHECK(labels.count == 2);
    CHECK(strcmp(bongo_cat_mver_label(&labels, "l2d_expression", 0),
        default_expression) == 0);
    CHECK(bongo_cat_mver_label(&labels, "l2d_expression", 1) == NULL);
    CHECK(strcmp(bongo_cat_mver_label(&labels, "l2d_motion_lockhand", 0),
        keyboard_hidden) == 0);
    CHECK(bongo_cat_model_remove_tree(root, NULL));
    SDL_free(temporary);
}

static void metadata_backfills_labels(void) {
    static const char keyboard_hidden[] =
        "\xE9\x94\xAE\xE7\x9B\x98\xE6\xB6\x88\xE5\xA4\xB1";
    char *temporary = SDL_GetCurrentDirectory();
    CHECK(temporary != NULL);
    if (!temporary) return;
    char root[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
    snprintf(root, sizeof(root), "%s/bongo-cat-metadata-%llu", temporary,
        (unsigned long long)SDL_GetTicksNS());
    CHECK(SDL_CreateDirectory(root));
    CHECK(child(path, sizeof(path), root, ".bongo-cat-adapter.json", false));
    CHECK(write_text(path, "{\"schemaVersion\":1,"
        "\"kind\":\"bongo-cat-runtime-adapter\",\"bindings\":["
        "{\"kind\":\"motion\",\"group\":\"CAT_motion_lock\",\"index\":0,"
        "\"shortcut\":\"Alt+1\",\"label\":\"\xE9\x94\xAE\xE7\x9B\x98\xE6\xB6\x88\xE5\xA4\xB1\"},"
        "{\"kind\":\"motion\",\"group\":\"CAT_motion_lock\",\"index\":1,"
        "\"shortcut\":\"Alt+2\",\"label\":\"Imported\"},"
        "{\"kind\":\"expression\",\"index\":2,\"shortcut\":\"Alt+3\","
        "\"label\":\"Expression label\"}]}"));
    BongoCatApp *app = calloc(1, sizeof(*app));
    CHECK(app != NULL);
    if (app) {
        app->settings.behavior_shortcut_count = 2;
        snprintf(app->settings.behavior_shortcuts[0].id,
            sizeof(app->settings.behavior_shortcuts[0].id),
            "model:motion:CAT_motion_lock:0");
        snprintf(app->settings.behavior_shortcuts[0].shortcut,
            sizeof(app->settings.behavior_shortcuts[0].shortcut), "Control+1");
        snprintf(app->settings.behavior_shortcuts[1].id,
            sizeof(app->settings.behavior_shortcuts[1].id),
            "model:motion:CAT_motion_lock:1");
        snprintf(app->settings.behavior_shortcuts[1].label,
            sizeof(app->settings.behavior_shortcuts[1].label), "Custom name");
        bongo_cat_import_apply_metadata(app, "model", root);
        CHECK(app->settings.behavior_shortcut_count == 3);
        CHECK(strcmp(app->settings.behavior_shortcuts[0].shortcut, "Control+1") == 0);
        CHECK(strcmp(app->settings.behavior_shortcuts[0].label, keyboard_hidden) == 0);
        CHECK(strcmp(app->settings.behavior_shortcuts[1].label, "Custom name") == 0);
        CHECK(strcmp(app->settings.behavior_shortcuts[2].id,
            "model:expression:2") == 0);
        CHECK(strcmp(app->settings.behavior_shortcuts[2].label,
            "Expression label") == 0);
        free(app);
    }
    CHECK(bongo_cat_model_remove_tree(root, NULL));
    SDL_free(temporary);
}

static bool range_has(const uint32_t *ranges, uint32_t rune) {
    for (size_t i = 0; ranges[i]; i += 2)
        if (rune >= ranges[i] && rune <= ranges[i + 1]) return true;
    return false;
}

static void behavior_labels_add_font_glyphs(void) {
    static const unsigned char custom_label[] = {
        0xe7, 0x8c, 0xab, 0xe5, 0x92, 0xaa, 0xe5, 0xbd,
        0xa2, 0xe6, 0x80, 0x81, 0
    };
    BongoCatApp *app = calloc(1, sizeof(*app));
    CHECK(app != NULL);
    if (!app) return;
    snprintf(app->behaviors.entries[0].label,
        sizeof(app->behaviors.entries[0].label),
        "\xE9\x94\xAE\xE7\x9B\x98\xE6\xB6\x88\xE5\xA4\xB1");
    app->behaviors.count = 1;
    memcpy(app->settings.behavior_shortcuts[0].label, custom_label,
        sizeof(custom_label));
    app->settings.behavior_shortcut_count = 1;
    uint32_t ranges[64] = {0x20, 0x7e, 0};
    bongo_cat_preferences_model_glyphs(app, ranges,
        sizeof(ranges) / sizeof(ranges[0]));
    CHECK(range_has(ranges, 0x952e));
    CHECK(range_has(ranges, 0x76d8));
    CHECK(range_has(ranges, 0x6d88));
    CHECK(range_has(ranges, 0x5931));
    CHECK(range_has(ranges, 0x732b));
    CHECK(range_has(ranges, 0x54aa));
    CHECK(range_has(ranges, 0x5f62));
    CHECK(range_has(ranges, 0x6001));
    free(app);
}

static void font_reload_defers_during_frame(void) {
    BongoCatPreferences value = {0};
    value.ui_initialized = true;
    value.ui.frame_building = true;
    CHECK(bongo_cat_preferences_reload_fonts(&value));
    CHECK(value.font_reload_pending);
    CHECK(value.render_dirty);
    BongoCatUIBackend backend = {0};
    backend.frame_building = true;
    CHECK(!bongo_cat_ui_font_atlas_reload(&backend, NULL, NULL,
        NULL, NULL, NULL, NULL, NULL, 1.0f));
}

static void model_visual_expires_without_window(void) {
    BongoCatPreferences value = {0};
    value.model_load_visual_active = true;
    value.model_load_visual_started_ns = SDL_GetTicksNS() -
        BONGO_CAT_MODEL_LOAD_VISUAL_DURATION_NS;
    CHECK(!bongo_cat_preferences_needs_frame(&value));
    CHECK(!value.model_load_visual_active && value.model_load_progress == 1.0f);
}

static void model_visual_curve(void) {
    BongoCatPreferences value = {0};
    bongo_cat_preferences_model_visual_begin(&value, "curve");
    value.model_loading = true;
    uint64_t now = SDL_GetTicksNS();
    value.model_load_visual_started_ns = now -
        BONGO_CAT_MODEL_LOAD_VISUAL_RAMP_NS;
    float ramp = bongo_cat_preferences_model_visual_progress(&value, "curve");
    CHECK(ramp > .79f && ramp < .81f);
    value.model_load_visual_started_ns = now - 4000000000ull;
    float waiting = bongo_cat_preferences_model_visual_progress(&value, "curve");
    CHECK(waiting > .86f && waiting < .89f);
    value.model_loading = false;
    value.model_load_visual_completion_ns = SDL_GetTicksNS() -
        BONGO_CAT_MODEL_LOAD_VISUAL_COMPLETE_NS;
    float complete = bongo_cat_preferences_model_visual_progress(&value, "curve");
    CHECK(complete == 1.0f && !value.model_load_visual_active);
}

static void check_portable_images(const char *root,
    const char *const *relative, size_t count) {
    char path[BONGO_CAT_PATH_CAP];
    for (size_t i = 0; i < count; ++i) {
        CHECK(child(path, sizeof(path), root, relative[i], false));
        CHECK(bongo_cat_path_is_file(path));
        CHECK(bongo_cat_image_info(path, NULL, NULL));
    }
}

static void tauri_exact_discovery(void) {
    char *temporary = SDL_GetCurrentDirectory();
    CHECK(temporary != NULL);
    if (!temporary) return;
    char root[BONGO_CAT_PATH_CAP], model[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
    snprintf(root, sizeof(root), "%s/bongo-cat-tauri-%llu", temporary,
        (unsigned long long)SDL_GetTicksNS());
    CHECK(SDL_CreateDirectory(root));
    CHECK(child(model, sizeof(model), root, "keyboard", true));
    CHECK(child(path, sizeof(path), model, "cat.model3.json", false));
    CHECK(write_text(path, "{\"Version\":3,\"FileReferences\":{"
        "\"Moc\":\"cat.moc3\",\"Textures\":["
        "\"resources/model-texture.png\"]}}"));
    CHECK(child(path, sizeof(path), model, "cat.moc3", false));
    CHECK(write_text(path, "MOC3"));
    char tray[BONGO_CAT_PATH_CAP];
    snprintf(tray, sizeof(tray), "%s/resources/assets/tray.png",
        BONGO_CAT_NATIVE_SOURCE_DIR);
    char resources[BONGO_CAT_PATH_CAP], left_keys[BONGO_CAT_PATH_CAP];
    char right_keys[BONGO_CAT_PATH_CAP], east[BONGO_CAT_PATH_CAP];
    char numeric[BONGO_CAT_PATH_CAP], model_texture[BONGO_CAT_PATH_CAP];
    CHECK(child(resources, sizeof(resources), model, "resources", true));
    CHECK(child(model_texture, sizeof(model_texture), resources,
        "model-texture.png", false));
    CHECK(SDL_CopyFile(tray, model_texture));
    CHECK(child(path, sizeof(path), resources, "background.png", false));
    CHECK(SDL_CopyFile(tray, path));
    CHECK(child(path, sizeof(path), resources, "cover.png", false));
    CHECK(SDL_CopyFile(tray, path));
    CHECK(child(left_keys, sizeof(left_keys), resources, "left-keys", true));
    CHECK(child(numeric, sizeof(numeric), left_keys, "24.png", false));
    CHECK(SDL_CopyFile(tray, numeric));
    CHECK(child(right_keys, sizeof(right_keys), resources, "right-keys", true));
    CHECK(child(east, sizeof(east), right_keys, "East.png", false));
    CHECK(SDL_CopyFile(tray, east));
    char nested[BONGO_CAT_PATH_CAP], nested_resources[BONGO_CAT_PATH_CAP];
    CHECK(child(nested, sizeof(nested), model, "nested", true));
    CHECK(child(nested_resources, sizeof(nested_resources), nested,
        "resources", true));
    CHECK(child(path, sizeof(path), nested_resources, "keep.bin", false));
    CHECK(write_text(path, "keep"));
    BongoCatImportDiscovery discovery = {0};
    BongoCatError error = {0};
    CHECK(bongo_cat_import_tauri_discover_exact(model, &discovery, &error) == 1);
    CHECK(discovery.count == 1 &&
        discovery.candidates[0].format == BONGO_CAT_IMPORT_TAURI &&
        discovery.candidates[0].mode == BONGO_CAT_MODE_GAMEPAD &&
        discovery.candidates[0].gamepad_buttons);
    CHECK(bongo_cat_path_remove(east));
    CHECK(child(path, sizeof(path), right_keys, "KeyA.png", false));
    CHECK(SDL_CopyFile(tray, path));
    BongoCatImportDiscovery keyboard = {0};
    CHECK(bongo_cat_import_tauri_discover_exact(model, &keyboard, &error) == 1);
    CHECK(keyboard.count == 1 &&
        keyboard.candidates[0].mode == BONGO_CAT_MODE_KEYBOARD &&
        !keyboard.candidates[0].gamepad_buttons);
    char models_root[BONGO_CAT_PATH_CAP];
    CHECK(child(models_root, sizeof(models_root), root, "models", true));
    BongoCatImportReceipt receipt = {0};
    CHECK(bongo_cat_import_install(model, models_root, &receipt, &error) ==
        BONGO_CAT_OK);
    CHECK(receipt.count == 1 && receipt.installed_count == 1 &&
        strcmp(receipt.ids[0], "keyboard") == 0);
    char package[BONGO_CAT_PATH_CAP];
    CHECK(child(package, sizeof(package), models_root, receipt.ids[0], false));
    CHECK(child(path, sizeof(path), package, "config.json", false) &&
        bongo_cat_path_is_file(path));
    yyjson_doc *normalized = bongo_cat_json_read_file(path, 0, NULL);
    yyjson_val *normalized_root = normalized
        ? yyjson_doc_get_root(normalized) : NULL;
    yyjson_val *normalized_mode = yyjson_obj_get(normalized_root, "keyboard");
    yyjson_val *normalized_left = yyjson_obj_get(normalized_mode, "lefthand");
    yyjson_val *window_size = yyjson_obj_get(
        yyjson_obj_get(normalized_root, "decoration"), "window_size");
    int expected_width = 0, expected_height = 0;
    CHECK(bongo_cat_image_info(tray, &expected_width, &expected_height));
    CHECK(yyjson_get_int(yyjson_obj_get(normalized_root, "mode")) == 2 &&
        yyjson_is_arr(normalized_left) &&
        yyjson_is_arr(yyjson_obj_get(normalized_mode, "righthand")) &&
        yyjson_get_int(yyjson_arr_get(yyjson_arr_get(normalized_left, 0), 0)) ==
            24 &&
        yyjson_get_int(yyjson_arr_get(window_size, 0)) == expected_width &&
        yyjson_get_int(yyjson_arr_get(window_size, 1)) == expected_height);
    yyjson_doc_free(normalized);
    CHECK(child(path, sizeof(path), package,
        "img/keyboard/cat_model/cat.model3.json", false) &&
        bongo_cat_path_is_file(path));
    CHECK(child(path, sizeof(path), package,
        "img/keyboard/cat_model/resources/model-texture.png", false) &&
        bongo_cat_path_is_file(path));
    CHECK(child(path, sizeof(path), package,
        "img/keyboard/cat_model/resources/right-keys", false) &&
        !bongo_cat_path_is_dir(path));
    CHECK(child(path, sizeof(path), package,
        "img/keyboard/cat_model/nested/resources/keep.bin", false) &&
        bongo_cat_path_is_file(path));
    static const char *const keyboard_runtime[] = {
        "img/keyboard/lefthand/leftup.png",
        "img/keyboard/righthand/rightup.png"
    };
    check_portable_images(package, keyboard_runtime,
        sizeof(keyboard_runtime) / sizeof(keyboard_runtime[0]));
    CHECK(child(path, sizeof(path), package, ".bongo-cat-adapter", false) &&
        !bongo_cat_path_is_dir(path));
    CHECK(child(path, sizeof(path), package, ".bongo-cat-package.json", false) &&
        !bongo_cat_path_is_file(path));
    BongoCatApp *app = calloc(1, sizeof(*app));
    CHECK(app != NULL);
    if (app) {
        snprintf(app->models_root, sizeof(app->models_root), "%s", models_root);
        CHECK(child(app->cache_root, sizeof(app->cache_root), root, "cache",
            true));
        bongo_cat_models_init(&app->models);
        CHECK(bongo_cat_import_installed_models(app, models_root, &error) ==
            BONGO_CAT_OK);
        CHECK(app->models.count == 1 &&
            app->models.entries[0].source_format == BONGO_CAT_MODEL_SOURCE_MVER &&
            strcmp(app->models.entries[0].storage_directory, package) == 0 &&
            strcmp(app->models.entries[0].adapter_directory, package) != 0);
        free(app);
    }
    BongoCatImportCandidate portable = keyboard.candidates[0], installed = {0};
    char portable_root[BONGO_CAT_PATH_CAP];
    portable.mode = BONGO_CAT_MODE_STANDARD;
    portable.gamepad_buttons = false;
    CHECK(child(portable_root, sizeof(portable_root), root,
        "portable-standard", false));
    error = (BongoCatError){0};
    CHECK(bongo_cat_import_tauri_convert_to_mver(&portable, portable_root,
        &installed, &error));
    static const char *const standard_runtime[] = {
        "img/standard/arm.png", "img/standard/up.png",
        "img/standard/mousebg.png", "img/standard/l2dmousebg.png",
        "img/standard/mouse.png", "img/standard/mouse_left.png",
        "img/standard/mouse_right.png", "img/standard/mouse_side.png",
        "img/standard/tabletbg.png", "img/standard/l2dtabletbg.png",
        "img/standard/tablet.png", "img/standard/tablet_left.png",
        "img/standard/tablet_right.png"
    };
    check_portable_images(portable_root, standard_runtime,
        sizeof(standard_runtime) / sizeof(standard_runtime[0]));
    CHECK(child(path, sizeof(path), portable_root, "img/standard/bg.png",
        false) && !bongo_cat_path_is_file(path));
    char adapter_root[BONGO_CAT_PATH_CAP];
    CHECK(child(adapter_root, sizeof(adapter_root), root,
        "portable-standard-adapter", true));
    error = (BongoCatError){0};
    CHECK(bongo_cat_import_prepare_adapter(&installed, adapter_root, &error));
    CHECK(child(path, sizeof(path), adapter_root,
        BONGO_CAT_MODEL_ADAPTER_FILE, false));
    yyjson_doc *adapter = bongo_cat_json_read_file(path, 0, NULL);
    yyjson_val *pointer = adapter ? yyjson_obj_get(
        yyjson_doc_get_root(adapter), "standardPointer") : NULL;
    CHECK(yyjson_is_obj(pointer) &&
        !yyjson_get_bool(yyjson_obj_get(pointer, "enabled")));
    yyjson_doc_free(adapter);
    CHECK(child(path, sizeof(path), adapter_root,
        "resources/mver-pointer/arm.png", false) &&
        !bongo_cat_path_is_file(path));
    portable.mode = BONGO_CAT_MODE_GAMEPAD;
    portable.gamepad_buttons = true;
    installed = (BongoCatImportCandidate){0};
    CHECK(child(portable_root, sizeof(portable_root), root,
        "portable-gamepad", false));
    error = (BongoCatError){0};
    CHECK(bongo_cat_import_tauri_convert_to_mver(&portable, portable_root,
        &installed, &error));
    static const char *const gamepad_runtime[] = {
        "img/gamepad/arm_L.png", "img/gamepad/arm_R.png",
        "img/gamepad/left_stick.png", "img/gamepad/left_stick_down.png",
        "img/gamepad/right_stick.png", "img/gamepad/right_stick_down.png",
        "img/gamepad/lefthand/leftup.png",
        "img/gamepad/righthand/rightup.png"
    };
    check_portable_images(portable_root, gamepad_runtime,
        sizeof(gamepad_runtime) / sizeof(gamepad_runtime[0]));
    CHECK(bongo_cat_model_remove_tree(root, NULL));
    SDL_free(temporary);
}

int main(void) {
    failures += test_preferences_text();
    CHECK(chord("[17,65]", true, "Control+A"));
    CHECK(chord("[0]", true, "Gamepad:South"));
    CHECK(chord("[15]", true, "Gamepad:Select"));
    CHECK(chord("[8]", false, "Backspace"));
    CHECK(chord("[24]", false, "24"));
    CHECK(chord("[16,65]", false, "Shift+A"));
    CHECK(chord("[17,18,90]", false, "Control+Alt+Z"));
    CHECK(chord("[0]", false, NULL));
    CHECK(chord("[16]", false, NULL));
    CHECK(chord("[17,65,66]", true, NULL));
    BongoCatMverKeyNames modifier = bongo_cat_mver_device_names(16, 1, 2);
    CHECK(modifier.count == 1 && strcmp(modifier.items[0], "ShiftRight") == 0);
    BongoCatMverKeyNames dpad = bongo_cat_mver_gamepad_names(12);
    CHECK(dpad.count == 1 && strcmp(dpad.items[0], "DPadUp") == 0);
    BongoCatMverKeyNames numeric = bongo_cat_mver_device_names(24, 0, 1);
    CHECK(numeric.count == 1 && strcmp(numeric.generated, "24") == 0);
    labels_from_shortcut_rows();
    metadata_backfills_labels();
    failures += test_mver_missing_motion_groups();
    behavior_labels_add_font_glyphs();
    font_reload_defers_during_frame();
    model_visual_expires_without_window();
    model_visual_curve();
    failures += test_mver_nearby_identity();
    failures += test_mver_nearby_refresh();
    failures += test_model_import_identity();
    test_mver_container_discovery();
    tauri_exact_discovery();
    failures += test_slim_package();
    return failures ? 1 : 0;
}
