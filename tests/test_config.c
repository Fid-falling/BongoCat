#include "test.h"
#include "bongo_cat/config.h"
#include "bongo_cat/file.h"
#include "bongo_cat/model.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <string.h>

static void write_text(const char *path, const char *text) {
    FILE *file = bongo_cat_file_open(path, "wb");
    CHECK(file != NULL);
    if (file) { CHECK(fputs(text, file) >= 0); CHECK(fclose(file) == 0); }
}

static bool contains_text(const char *path, const char *needle) {
    char content[8192] = {0};
    FILE *file = bongo_cat_file_open(path, "rb");
    if (!file) return false;
    size_t length = fread(content, 1, sizeof(content) - 1, file);
    fclose(file);
    content[length] = '\0';
    return strstr(content, needle) != NULL;
}

static void check_defaults_and_validation(void) {
    BongoCatSettings settings;
    BongoCatSessionState session;
    bongo_cat_settings_defaults(&settings);
    bongo_cat_session_defaults(&session);
    CHECK(settings.model.max_fps == 60 && settings.model.mouse_centered);
    CHECK(settings.window.always_on_top && !settings.window.keep_in_screen);
    CHECK(!settings.window.obs_background);
    CHECK(settings.window.obs_background_color ==
        BONGO_CAT_OBS_BACKGROUND_GREEN);
    CHECK(session.window.visible && session.window.width == 612 &&
        session.window.height == 354);
    CHECK(strcmp(session.active_model_id, "standard") == 0);

    settings.model.max_fps = 900;
    settings.window.obs_background_color = BONGO_CAT_OBS_BACKGROUND_COLOR_COUNT;
    session.window.scale_percent = -2.0f;
    session.window.opacity_percent = 123.0f;
    bongo_cat_settings_validate(&settings);
    bongo_cat_session_validate(&session);
    CHECK(settings.model.max_fps == 240);
    CHECK(settings.window.obs_background_color ==
        BONGO_CAT_OBS_BACKGROUND_GREEN);
    CHECK(session.window.scale_percent == 10.0f);
    CHECK(session.window.opacity_percent == 100.0f);
}

static void check_shortcuts(void) {
    BongoCatSettings settings;
    bongo_cat_settings_defaults(&settings);
    memcpy(settings.shortcuts.visible_cat, "Control+B", sizeof("Control+B"));
    memcpy(settings.shortcuts.visible_preferences, "control+b",
        sizeof("control+b"));
    settings.behavior_shortcut_count = 2;
    memcpy(settings.behavior_shortcuts[0].id, "motion:0", sizeof("motion:0"));
    memcpy(settings.behavior_shortcuts[0].shortcut, "CONTROL+B",
        sizeof("CONTROL+B"));
    memcpy(settings.behavior_shortcuts[1].id, "motion:1", sizeof("motion:1"));
    memcpy(settings.behavior_shortcuts[1].shortcut, "Control+M",
        sizeof("Control+M"));
    bongo_cat_settings_validate(&settings);
    CHECK(strcmp(settings.shortcuts.visible_cat, "Control+B") == 0);
    CHECK(!settings.shortcuts.visible_preferences[0]);
    CHECK(!settings.behavior_shortcuts[0].shortcut[0]);
    CHECK(strcmp(settings.behavior_shortcuts[1].shortcut, "Control+M") == 0);
    CHECK(bongo_cat_settings_shortcut_conflicts(&settings, "control+b", NULL));
    CHECK(!bongo_cat_settings_shortcut_conflicts(&settings, "control+b",
        settings.shortcuts.visible_cat));
}

void test_config(void) {
    check_defaults_and_validation();
    check_shortcuts();
    const uint32_t colors[] = {0x00ff00, 0x0000ff, 0xff0000, 0xff00ff};
    for (int i = 0; i < BONGO_CAT_OBS_BACKGROUND_COLOR_COUNT; ++i)
        CHECK(bongo_cat_obs_background_color_rgb(i) == colors[i]);

    BongoCatSettings settings;
    BongoCatSessionState session;
    bongo_cat_settings_defaults(&settings);
    bongo_cat_session_defaults(&session);
    settings.model.max_fps = 30;
    settings.model.mirror = true;
    settings.model.mouse_centered = false;
    settings.window.pass_through = true;
    settings.window.obs_background = true;
    settings.window.obs_background_color = BONGO_CAT_OBS_BACKGROUND_BLUE;
    settings.app.language = BONGO_CAT_LANG_ZH_CN;
    memcpy(settings.extensions_json, "{\"example\":{\"enabled\":true}}",
        sizeof("{\"example\":{\"enabled\":true}}"));
    settings.behavior_shortcut_count = 1;
    memcpy(settings.behavior_shortcuts[0].id, "model:motion:Tap:0",
        sizeof("model:motion:Tap:0"));
    memcpy(settings.behavior_shortcuts[0].shortcut, "Control+1",
        sizeof("Control+1"));
    memcpy(settings.behavior_shortcuts[0].label, "Happy tap",
        sizeof("Happy tap"));
    CHECK(bongo_cat_settings_set_model_label(&settings, "model", "Display"));
    session.window.x = -321;
    session.window.position_known = true;
    session.window.opacity_percent = 75.0f;
    memcpy(session.active_model_id, "model", sizeof("model"));

    const char *settings_path = "bongocat-settings.json";
    const char *session_path = "bongocat-session.json";
    BongoCatError error = {0};
    CHECK(bongo_cat_settings_save(settings_path, &settings, &error) ==
        BONGO_CAT_OK);
    CHECK(bongo_cat_session_save(session_path, &session, &error) ==
        BONGO_CAT_OK);
    CHECK(contains_text(settings_path, "\"format\": \"bongocat/settings\""));
    CHECK(contains_text(settings_path, "\"captureBackground\": true"));
    CHECK(contains_text(settings_path, "\"example\""));
    CHECK(!contains_text(settings_path, "activeModelId"));
    CHECK(contains_text(session_path, "\"format\": \"bongocat/session\""));
    CHECK(contains_text(session_path, "\"activeModelId\": \"model\""));
    CHECK(!contains_text(session_path, "clickThrough"));

    BongoCatSettings loaded_settings;
    BongoCatSessionState loaded_session;
    bongo_cat_settings_defaults(&loaded_settings);
    bongo_cat_session_defaults(&loaded_session);
    CHECK(bongo_cat_settings_load(settings_path, &loaded_settings, &error) ==
        BONGO_CAT_OK);
    CHECK(bongo_cat_session_load(session_path, &loaded_session, &error) ==
        BONGO_CAT_OK);
    CHECK(loaded_settings.model.max_fps == 30 && loaded_settings.model.mirror);
    CHECK(loaded_settings.window.pass_through &&
        loaded_settings.window.obs_background);
    CHECK(loaded_settings.app.language == BONGO_CAT_LANG_ZH_CN);
    CHECK(strstr(loaded_settings.extensions_json,
        "\"enabled\":true") != NULL);
    CHECK(strcmp(bongo_cat_model_name(&loaded_settings,
        &(BongoCatModelEntry){.id = "model", .display_name = "Nearby"}),
        "Display") == 0);
    CHECK(loaded_session.window.x == -321 &&
        loaded_session.window.opacity_percent == 75.0f);
    CHECK(strcmp(loaded_session.active_model_id, "model") == 0);

    const char *unsupported = "bongocat-unsupported.json";
    write_text(unsupported,
        "{\"format\":\"bongocat/settings\",\"schemaVersion\":2}");
    CHECK(bongo_cat_settings_load(unsupported, &loaded_settings, &error) ==
        BONGO_CAT_ERROR_FORMAT);
    write_text(unsupported,
        "{\"format\":\"bongocat/session\",\"schemaVersion\":2}");
    CHECK(bongo_cat_session_load(unsupported, &loaded_session, &error) ==
        BONGO_CAT_ERROR_FORMAT);

    CHECK(bongo_cat_file_remove(settings_path));
    CHECK(bongo_cat_file_remove(session_path));
    CHECK(bongo_cat_file_remove(unsupported));
}
