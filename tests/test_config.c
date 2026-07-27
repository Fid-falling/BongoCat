#include "test.h"
#include "bongo_cat_neo/config.h"
#include "bongo_cat_neo/file.h"
#include "bongo_cat_neo/path.h"

#include <stdio.h>
#include <string.h>

static void write_text(const char *path, const char *text) {
    FILE *file = bongo_cat_neo_file_open(path, "wb");
    CHECK(file != NULL);
    if (file) { CHECK(fputs(text, file) >= 0); CHECK(fclose(file) == 0); }
}

void test_config(void) {
    BongoCatNeoConfig value;
    bongo_cat_neo_config_defaults(&value);
    CHECK(value.model.max_fps == 60);
    CHECK(value.window.width == 612 && value.window.height == 354);
    CHECK(value.window.visible && value.window.always_on_top);

    value.model.max_fps = 900;
    value.window.scale_percent = -2.0f;
    value.window.opacity_percent = 123.0f;
    bongo_cat_neo_config_validate(&value);
    CHECK(value.model.max_fps == 240);
    CHECK(value.window.scale_percent == 10.0f);
    CHECK(value.window.opacity_percent == 100.0f);

    value.model.max_fps = 30;
    value.model.mirror = true;
    value.window.pass_through = true;
    value.window.x = -321;
    value.window.opacity_percent = 75.0f;
    value.app.language = BONGO_CAT_NEO_LANG_ZH_CN;
    memcpy(value.current_model, "keyboard", sizeof("keyboard"));
    value.current_mode = BONGO_CAT_NEO_MODE_KEYBOARD;
    value.behavior_shortcut_count = 1;
    memcpy(value.behavior_shortcuts[0].id, "keyboard:motion:Tap:0",
        sizeof("keyboard:motion:Tap:0"));
    memcpy(value.behavior_shortcuts[0].shortcut, "Control+1", sizeof("Control+1"));

    const char *preferences = "bongo-cat-neo-\xE5\x81\x8F\xE5\xA5\xBD.json";
    const char *session = "bongo-cat-neo-\xE4\xBC\x9A\xE8\xAF\x9D.json";
    BongoCatNeoError error = {0};
    CHECK(bongo_cat_neo_preferences_save(preferences, &value, &error) == BONGO_CAT_NEO_OK);
    CHECK(bongo_cat_neo_session_save(session, &value, &error) == BONGO_CAT_NEO_OK);
    CHECK(bongo_cat_neo_path_is_file(preferences));
    CHECK(bongo_cat_neo_path_is_file(session));

    BongoCatNeoConfig loaded;
    bongo_cat_neo_config_defaults(&loaded);
    CHECK(bongo_cat_neo_preferences_load(preferences, &loaded, &error) == BONGO_CAT_NEO_OK);
    CHECK(loaded.model.max_fps == 30 && loaded.model.mirror);
    CHECK(loaded.window.pass_through);
    CHECK(loaded.window.x == 0);
    CHECK(loaded.window.opacity_percent == 100.0f);
    CHECK(loaded.app.language == BONGO_CAT_NEO_LANG_ZH_CN);
    CHECK(strcmp(loaded.current_model, "standard") == 0);
    CHECK(loaded.behavior_shortcut_count == 1);
    CHECK(strcmp(loaded.behavior_shortcuts[0].shortcut, "Control+1") == 0);

    CHECK(bongo_cat_neo_session_load(session, &loaded, &error) == BONGO_CAT_NEO_OK);
    CHECK(loaded.window.x == -321);
    CHECK(loaded.window.opacity_percent == 75.0f);
    CHECK(strcmp(loaded.current_model, "keyboard") == 0);
    CHECK(loaded.current_mode == BONGO_CAT_NEO_MODE_GAMEPAD);

    const char *legacy = "bongo-cat-neo-legacy.json";
    write_text(legacy, "{\"schemaVersion\":2,\"model\":{\"maxFPS\":1}}");
    int preserved_fps = loaded.model.max_fps;
    CHECK(bongo_cat_neo_preferences_load(legacy, &loaded, &error) ==
        BONGO_CAT_NEO_ERROR_FORMAT);
    CHECK(loaded.model.max_fps == preserved_fps);

    const char *broken_session = "bongo-cat-neo-broken-session.json";
    write_text(broken_session, "{ invalid");
    bool preserved_mirror = loaded.model.mirror;
    CHECK(bongo_cat_neo_session_load(broken_session, &loaded, &error) ==
        BONGO_CAT_NEO_ERROR_FORMAT);
    CHECK(loaded.model.mirror == preserved_mirror);

    CHECK(bongo_cat_neo_file_remove(preferences));
    CHECK(bongo_cat_neo_file_remove(session));
    CHECK(bongo_cat_neo_file_remove(legacy));
    CHECK(bongo_cat_neo_file_remove(broken_session));
}
