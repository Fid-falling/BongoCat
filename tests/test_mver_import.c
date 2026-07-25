#include "model_import_mver_internal.h"
#include "model_storage.h"
#include "runtime.h"
#include "bongo_cat_neo/file.h"
#include "bongo_cat_neo/path.h"

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

static bool chord(const char *json, bool gamepad, const char *expected) {
    yyjson_doc *document = yyjson_read(json, strlen(json), 0);
    BongoCatNeoImportCandidate candidate = {0};
    candidate.gamepad_buttons = gamepad;
    char output[BONGO_CAT_NEO_SHORTCUT_CAP];
    bool ok = document && bongo_cat_neo_mver_chord(&candidate,
        yyjson_doc_get_root(document), output, sizeof(output));
    bool matches = ok && expected && strcmp(output, expected) == 0;
    yyjson_doc_free(document);
    return expected ? matches : !ok;
}

static bool write_text(const char *path, const char *text) {
    FILE *file = bongo_cat_neo_file_open(path, "wb");
    bool ok = file && fwrite(text, 1, strlen(text), file) == strlen(text);
    if (file && fclose(file) != 0) ok = false;
    return ok;
}

static bool child(char *output, size_t capacity, const char *root,
    const char *name, bool directory) {
    return bongo_cat_neo_path_join(output, capacity, root, name) &&
        (!directory || SDL_CreateDirectory(output));
}

static bool portable_fixture(const char *root) {
    char image[BONGO_CAT_NEO_PATH_CAP], standard[BONGO_CAT_NEO_PATH_CAP];
    char hand[BONGO_CAT_NEO_PATH_CAP], model[BONGO_CAT_NEO_PATH_CAP];
    char path[BONGO_CAT_NEO_PATH_CAP], source[BONGO_CAT_NEO_PATH_CAP];
    if (!SDL_CreateDirectory(root) || !child(image, sizeof(image), root, "img", true) ||
        !child(standard, sizeof(standard), image, "standard", true) ||
        !child(hand, sizeof(hand), standard, "hand", true) ||
        !child(model, sizeof(model), standard, "cat_model", true)) return false;
    if (!child(path, sizeof(path), root, "config.json", false) ||
        !write_text(path, "{\"standard\":{\"keyboard\":[[65]],\"hand\":[[65]],"
            "\"l2d_expression\":[[65],[66]]}}"))
        return false;
    if (!child(path, sizeof(path), model, "cat.model3.json", false) ||
        !write_text(path, "{\"Version\":3,\"FileReferences\":{\"Moc\":\"cat.moc3\","
            "\"Textures\":[\"texture.png\"]}}") ||
        !child(path, sizeof(path), model, "cat.moc3", false) ||
        !write_text(path, "MOC3")) return false;
    snprintf(source, sizeof(source), "%s/resources/assets/tray.png",
        BONGO_CAT_NEO_NATIVE_SOURCE_DIR);
    if (!child(path, sizeof(path), model, "texture.png", false) ||
        !SDL_CopyFile(source, path) || !child(path, sizeof(path), hand, "0.png", false) ||
        !SDL_CopyFile(source, path) || !child(path, sizeof(path), standard, "bg.png", false) ||
        !SDL_CopyFile(source, path)) return false;
    return true;
}

static void portable_model(void) {
    char root[BONGO_CAT_NEO_PATH_CAP], package[BONGO_CAT_NEO_PATH_CAP];
    char data[BONGO_CAT_NEO_PATH_CAP], mode[BONGO_CAT_NEO_PATH_CAP];
    char bundle[BONGO_CAT_NEO_PATH_CAP], nested_package[BONGO_CAT_NEO_PATH_CAP];
    char variant[BONGO_CAT_NEO_PATH_CAP], variant_model[BONGO_CAT_NEO_PATH_CAP];
    char variant_img[BONGO_CAT_NEO_PATH_CAP], variant_standard[BONGO_CAT_NEO_PATH_CAP];
    char variant_hand[BONGO_CAT_NEO_PATH_CAP], source_hand[BONGO_CAT_NEO_PATH_CAP];
    char *temporary = SDL_GetCurrentDirectory();
    CHECK(temporary != NULL);
    if (!temporary) return;
    unsigned long long stamp = (unsigned long long)SDL_GetTicksNS();
    snprintf(root, sizeof(root), "%s/bongo-cat-neo-portable-%llu", temporary,
        stamp);
    snprintf(data, sizeof(data), "%s/bongo-cat-neo-portable-data-%llu", temporary,
        stamp);
    bongo_cat_neo_model_remove_tree(root, NULL);
    bongo_cat_neo_model_remove_tree(data, NULL);
    CHECK(SDL_CreateDirectory(root));
    CHECK(SDL_CreateDirectory(data));
    CHECK(child(package, sizeof(package), root, "app", true));
    CHECK(portable_fixture(package));
    CHECK(child(bundle, sizeof(bundle), root, "bundle", true));
    CHECK(child(nested_package, sizeof(nested_package), bundle, "model", true));
    CHECK(portable_fixture(nested_package));
    CHECK(child(variant, sizeof(variant), bundle, "variant", true));
    CHECK(child(variant_model, sizeof(variant_model), variant, "model", true));
    CHECK(child(variant_img, sizeof(variant_img), variant_model, "img", true));
    CHECK(child(variant_standard, sizeof(variant_standard), variant_img, "standard", true));
    CHECK(child(variant_hand, sizeof(variant_hand), variant_standard, "hand", true));
    CHECK(child(source_hand, sizeof(source_hand), nested_package,
        "img/standard/hand/0.png", false));
    CHECK(child(mode, sizeof(mode), variant_hand, "0.png", false) &&
        SDL_CopyFile(source_hand, mode));
    BongoCatNeoImportDiscovery discovery = {0};
    BongoCatNeoError error = {0};
    CHECK(bongo_cat_neo_import_mver_discover_exact(package, &discovery, &error) == 1);
    CHECK(discovery.count == 1);
    CHECK(child(mode, sizeof(mode), package, "img/standard", false));
    BongoCatNeoImportDiscovery nested = {0};
    CHECK(bongo_cat_neo_import_mver_discover_exact(mode, &nested, &error) == 0);
    CHECK(bongo_cat_neo_import_mver_discover(mode, &nested, &error) == 1);
    BongoCatNeoApp *app = calloc(1, sizeof(*app));
    CHECK(app != NULL);
    if (!app) { bongo_cat_neo_model_remove_tree(root, NULL); SDL_free(temporary); return; }
    bongo_cat_neo_config_defaults(&app->config);
    bongo_cat_neo_models_init(&app->models);
    snprintf(app->data_root, sizeof(app->data_root), "%s", data);
    CHECK(bongo_cat_neo_import_portable_mver(app, package, &error) == BONGO_CAT_NEO_OK);
    CHECK(app->models.count == 1);
    CHECK(app->models.entries[0].managed && !app->models.entries[0].preset);
    CHECK(strcmp(app->config.current_model, app->models.entries[0].id) == 0);
    char adapter_file[BONGO_CAT_NEO_PATH_CAP];
    CHECK(child(adapter_file, sizeof(adapter_file),
        app->models.entries[0].adapter_directory, "resources/left-keys/KeyA.png", false));
    CHECK(bongo_cat_neo_path_is_file(adapter_file));
    bongo_cat_neo_models_init(&app->models);
    CHECK(bongo_cat_neo_import_portable_mver(app, package, &error) == BONGO_CAT_NEO_OK);
    CHECK(app->models.count == 1);
    bongo_cat_neo_models_init(&app->models);
    CHECK(bongo_cat_neo_import_portable_mver(app, root, &error) == BONGO_CAT_NEO_OK);
    CHECK(app->models.count == 3);
    free(app);
    CHECK(bongo_cat_neo_model_remove_tree(root, NULL));
    CHECK(bongo_cat_neo_model_remove_tree(data, NULL));
    SDL_free(temporary);
}

int main(void) {
    CHECK(chord("[17,65]", true, "Control+A"));
    CHECK(chord("[0]", true, "Gamepad:South"));
    CHECK(chord("[15]", true, "Gamepad:Select"));
    CHECK(chord("[8]", false, "Backspace"));
    CHECK(chord("[16,65]", false, "Shift+A"));
    CHECK(chord("[17,18,90]", false, "Control+Alt+Z"));
    CHECK(chord("[0]", false, NULL));
    CHECK(chord("[16]", false, NULL));
    CHECK(chord("[17,65,66]", true, NULL));
    BongoCatNeoMverKeyNames modifier = bongo_cat_neo_mver_device_names(16, 1, 2);
    CHECK(modifier.count == 1 && strcmp(modifier.items[0], "ShiftRight") == 0);
    BongoCatNeoMverKeyNames dpad = bongo_cat_neo_mver_gamepad_names(12);
    CHECK(dpad.count == 1 && strcmp(dpad.items[0], "DPadUp") == 0);
    portable_model();
    return failures ? 1 : 0;
}
