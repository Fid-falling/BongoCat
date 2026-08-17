#include "model_import_mver_internal.h"
#include "model_storage.h"
#include "preferences_model_glyphs.h"
#include "preferences_state.h"
#include "ui_font_atlas.h"
#include "runtime.h"
#include "test_mver_support.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

static int failures;
#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #value); \
    failures++; \
} } while (0)

int test_preferences_text(void);
int test_mver_nearby_identity(void);
int test_model_import_identity(void);

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

static void container_discovery(void) {
    char root[BONGO_CAT_PATH_CAP], package[BONGO_CAT_PATH_CAP];
    char mode[BONGO_CAT_PATH_CAP];
    char bundle[BONGO_CAT_PATH_CAP], nested_package[BONGO_CAT_PATH_CAP];
    char variant[BONGO_CAT_PATH_CAP], variant_model[BONGO_CAT_PATH_CAP];
    char variant_img[BONGO_CAT_PATH_CAP], variant_standard[BONGO_CAT_PATH_CAP];
    char variant_hand[BONGO_CAT_PATH_CAP], source_hand[BONGO_CAT_PATH_CAP];
    char *temporary = SDL_GetCurrentDirectory();
    CHECK(temporary != NULL);
    if (!temporary) return;
    unsigned long long stamp = (unsigned long long)SDL_GetTicksNS();
    snprintf(root, sizeof(root), "%s/bongo-cat-container-%llu", temporary,
        stamp);
    bongo_cat_model_remove_tree(root, NULL);
    CHECK(SDL_CreateDirectory(root));
    CHECK(child(package, sizeof(package), root, "app", true));
    CHECK(mver_fixture(package));
    CHECK(child(bundle, sizeof(bundle), root, "bundle", true));
    CHECK(child(nested_package, sizeof(nested_package), bundle, "model", true));
    CHECK(mver_fixture(nested_package));
    CHECK(child(variant, sizeof(variant), bundle, "variant", true));
    CHECK(child(variant_model, sizeof(variant_model), variant, "model", true));
    CHECK(child(variant_img, sizeof(variant_img), variant_model, "img", true));
    CHECK(child(variant_standard, sizeof(variant_standard), variant_img, "standard", true));
    CHECK(child(variant_hand, sizeof(variant_hand), variant_standard, "hand", true));
    CHECK(child(source_hand, sizeof(source_hand), nested_package,
        "img/standard/hand/0.png", false));
    CHECK(child(mode, sizeof(mode), variant_hand, "0.png", false) &&
        SDL_CopyFile(source_hand, mode));
    char normalized[BONGO_CAT_PATH_CAP];
    BongoCatError source_error = {0};
    CHECK(bongo_cat_import_source_directory(mode, normalized,
        sizeof(normalized), &source_error) == BONGO_CAT_OK);
    CHECK(strcmp(normalized, variant_model) == 0);
    BongoCatImportDiscovery discovery = {0};
    BongoCatError error = {0};
    CHECK(bongo_cat_import_mver_discover_exact(package, &discovery, &error) == 1);
    CHECK(discovery.count == 1);
    CHECK(child(mode, sizeof(mode), package, "img/standard", false));
    BongoCatImportDiscovery nested = {0};
    CHECK(bongo_cat_import_mver_discover_exact(mode, &nested, &error) == 0);
    CHECK(bongo_cat_import_mver_discover(mode, &nested, &error) == 1);
    BongoCatImportDiscovery selected_container = {0};
    CHECK(bongo_cat_import_discover(root, &selected_container, &error));
    CHECK(selected_container.count == 3);
    size_t patch_count = 0;
    for (size_t i = 0; i < selected_container.count; ++i)
        if (selected_container.candidates[i].format == BONGO_CAT_IMPORT_MVER_PATCH)
            patch_count++;
    CHECK(patch_count == 1);
    CHECK(bongo_cat_model_remove_tree(root, NULL));
    SDL_free(temporary);
}

static void tauri_exact_discovery(void) {
    char *temporary = SDL_GetCurrentDirectory();
    CHECK(temporary != NULL);
    if (!temporary) return;
    char root[BONGO_CAT_PATH_CAP], model[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
    snprintf(root, sizeof(root), "%s/bongo-cat-tauri-%llu", temporary,
        (unsigned long long)SDL_GetTicksNS());
    CHECK(SDL_CreateDirectory(root));
    CHECK(child(model, sizeof(model), root, "model", true));
    CHECK(child(path, sizeof(path), model, "cat.model3.json", false));
    CHECK(write_text(path, "{\"Version\":3,\"FileReferences\":{"
        "\"Moc\":\"cat.moc3\",\"Textures\":[\"texture.png\"]}}"));
    CHECK(child(path, sizeof(path), model, "cat.moc3", false));
    CHECK(write_text(path, "MOC3"));
    CHECK(child(path, sizeof(path), model, "texture.png", false));
    char tray[BONGO_CAT_PATH_CAP];
    snprintf(tray, sizeof(tray), "%s/resources/assets/tray.png",
        BONGO_CAT_NATIVE_SOURCE_DIR);
    CHECK(SDL_CopyFile(tray, path));
    BongoCatImportDiscovery discovery = {0};
    BongoCatError error = {0};
    CHECK(bongo_cat_import_tauri_discover_exact(model, &discovery, &error) == 1);
    CHECK(discovery.count == 1 &&
        discovery.candidates[0].format == BONGO_CAT_IMPORT_TAURI);
    CHECK(bongo_cat_model_remove_tree(root, NULL));
    SDL_free(temporary);
}

int main(void) {
    failures += test_preferences_text();
    CHECK(chord("[17,65]", true, "Control+A"));
    CHECK(chord("[0]", true, "Gamepad:South"));
    CHECK(chord("[15]", true, "Gamepad:Select"));
    CHECK(chord("[8]", false, "Backspace"));
    CHECK(chord("[16,65]", false, "Shift+A"));
    CHECK(chord("[17,18,90]", false, "Control+Alt+Z"));
    CHECK(chord("[0]", false, NULL));
    CHECK(chord("[16]", false, NULL));
    CHECK(chord("[17,65,66]", true, NULL));
    BongoCatMverKeyNames modifier = bongo_cat_mver_device_names(16, 1, 2);
    CHECK(modifier.count == 1 && strcmp(modifier.items[0], "ShiftRight") == 0);
    BongoCatMverKeyNames dpad = bongo_cat_mver_gamepad_names(12);
    CHECK(dpad.count == 1 && strcmp(dpad.items[0], "DPadUp") == 0);
    labels_from_shortcut_rows();
    metadata_backfills_labels();
    behavior_labels_add_font_glyphs();
    font_reload_defers_during_frame();
    model_visual_expires_without_window();
    model_visual_curve();
    failures += test_mver_nearby_identity();
    failures += test_model_import_identity();
    container_discovery();
    tauri_exact_discovery();
    return failures ? 1 : 0;
}
