#include "test.h"
#include "bongo_cat_neo/config.h"
#include "bongo_cat_neo/file.h"
#include "bongo_cat_neo/path.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void test_config(void) {
    BongoCatNeoConfig value;
    bongo_cat_neo_config_defaults(&value);
    CHECK(value.model.max_fps == 60);
    CHECK(value.window.width == 612);
    CHECK(value.window.height == 354);
    CHECK(value.window.visible && value.window.always_on_top);
    CHECK(value.current_mode == BONGO_CAT_NEO_MODE_GAMEPAD);

    value.model.max_fps = 900;
    value.window.scale_percent = -2.0f;
    value.window.opacity_percent = 123.0f;
    bongo_cat_neo_config_validate(&value);
    CHECK(value.model.max_fps == 240);
    CHECK(value.window.scale_percent == 10.0f);
    CHECK(value.window.opacity_percent == 100.0f);
    value.window.opacity_percent = -1.0f;
    bongo_cat_neo_config_validate(&value);
    CHECK(value.window.opacity_percent == 10.0f);

    value.model.max_fps = 30;
    value.window.x = -321;
    value.app.language = BONGO_CAT_NEO_LANG_ZH_CN;
    memcpy(value.current_model, "keyboard", sizeof("keyboard"));
    value.current_mode = BONGO_CAT_NEO_MODE_KEYBOARD;
    value.behavior_shortcut_count = 1;
    memcpy(value.behavior_shortcuts[0].id, "keyboard:motion:Tap:0",
        sizeof("keyboard:motion:Tap:0"));
    memcpy(value.behavior_shortcuts[0].shortcut, "Control+1", sizeof("Control+1"));
    BongoCatNeoError error = {0};
    const char *path = "bongo-cat-neo-\xE8\xAE\xBE\xE7\xBD\xAE.json";
    CHECK(bongo_cat_neo_config_save(path, &value, &error) == BONGO_CAT_NEO_OK);
    CHECK(bongo_cat_neo_path_is_file(path));
    char found[BONGO_CAT_NEO_PATH_CAP];
    CHECK(bongo_cat_neo_path_find_suffix(".", "\xE8\xAE\xBE\xE7\xBD\xAE.json",
        found, sizeof(found)));
    CHECK(strcmp(found, path) == 0);

    BongoCatNeoConfig loaded;
    CHECK(bongo_cat_neo_config_load(path, &loaded, &error) == BONGO_CAT_NEO_OK);
    CHECK(loaded.model.max_fps == 30);
    CHECK(loaded.window.x == -321);
    CHECK(loaded.app.language == BONGO_CAT_NEO_LANG_ZH_CN);
    CHECK(strcmp(loaded.current_model, "keyboard") == 0);
    CHECK(loaded.current_mode == BONGO_CAT_NEO_MODE_KEYBOARD);
    CHECK(loaded.behavior_shortcut_count == 1);
    CHECK(strcmp(loaded.behavior_shortcuts[0].shortcut, "Control+1") == 0);
    CHECK(bongo_cat_neo_file_remove(path));

    const char *old_schema_path = "bongo-cat-neo-legacy.json";
    FILE *legacy = bongo_cat_neo_file_open(old_schema_path, "wb");
    CHECK(legacy != NULL);
    if (legacy) {
        fputs("{\"schemaVersion\":1,\"window\":{\"visible\":false,"
            "\"alwaysOnTop\":false}}", legacy);
        fclose(legacy);
    }
    CHECK(bongo_cat_neo_config_load(old_schema_path, &loaded, &error) == BONGO_CAT_NEO_OK);
    CHECK(loaded.schema_version == 2);
    CHECK(loaded.window.visible && loaded.window.always_on_top);
    CHECK(bongo_cat_neo_file_remove(old_schema_path));
}
