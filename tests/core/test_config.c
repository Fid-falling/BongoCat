#include "test.h"
#include "bongo_cat/config.h"
#include "bongo_cat/file.h"
#include "bongo_cat/model.h"
#include "bongo_cat/path.h"

#include <math.h>
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
    static BongoCatSettings settings;
    static BongoCatSessionState session;
    bongo_cat_settings_defaults(&settings);
    bongo_cat_session_defaults(&session);
    CHECK(settings.model.max_fps == 60 && settings.model.mouse_centered &&
        !settings.model.multiple_pets);
    CHECK(settings.window.always_on_top && !settings.window.keep_in_screen);
    CHECK(!settings.window.obs_background);
    CHECK(!settings.window.random_expression &&
        settings.window.random_expression_interval_seconds ==
        BONGO_CAT_DEFAULT_RANDOM_EXPRESSION_SECONDS);
    CHECK(settings.window.obs_background_color ==
        BONGO_CAT_OBS_BACKGROUND_GREEN);
    CHECK(session.window.visible && session.window.width == 612 &&
        session.window.height == 354 && session.window.content_width == 612 &&
        session.window.content_height == 354);
    CHECK(strcmp(session.active_model_id, "standard") == 0);
    CHECK(session.additional_model_count == 0);
    CHECK(bongo_cat_session_add_model(&session, "keyboard"));
    CHECK(bongo_cat_session_add_model(&session, "keyboard"));
    CHECK(bongo_cat_session_add_model(&session, "gamepad"));
    CHECK(session.additional_model_count == 2 &&
        bongo_cat_session_model_active(&session, "standard") &&
        bongo_cat_session_model_active(&session, "keyboard"));
    CHECK(bongo_cat_session_remove_model(&session, "keyboard"));
    CHECK(session.additional_model_count == 1 &&
        !bongo_cat_session_model_active(&session, "keyboard"));
    bongo_cat_session_clear_additional_models(&session);
    settings.model.max_fps = 900;
    settings.window.obs_background_color = BONGO_CAT_OBS_BACKGROUND_COLOR_COUNT;
    settings.window.hide_delay_seconds = NAN;
    settings.window.random_expression_interval_seconds = NAN;
    session.window.scale_percent = -2.0f;
    session.window.opacity_percent = NAN;
    session.active_behavior_count = 3;
    memcpy(session.active_behaviors[0].model_id, "model",
        sizeof("model"));
    memcpy(session.active_behaviors[0].behavior_id, "model:motion:Tap:0",
        sizeof("model:motion:Tap:0"));
    session.active_behaviors[1] = session.active_behaviors[0];
    memcpy(session.active_behaviors[2].model_id, "other",
        sizeof("other"));
    bongo_cat_settings_validate(&settings);
    bongo_cat_session_validate(&session);
    CHECK(settings.model.max_fps == 240);
    CHECK(settings.window.hide_delay_seconds == 0.0f);
    CHECK(settings.window.random_expression_interval_seconds ==
        BONGO_CAT_DEFAULT_RANDOM_EXPRESSION_SECONDS);
    CHECK(settings.window.obs_background_color ==
        BONGO_CAT_OBS_BACKGROUND_GREEN);
    CHECK(session.window.scale_percent == 10.0f);
    CHECK(session.window.opacity_percent ==
        BONGO_CAT_DEFAULT_WINDOW_OPACITY_PERCENT);
    CHECK(session.active_behavior_count == 1);
    CHECK(strcmp(session.active_behaviors[0].model_id, "model") == 0);
}

static void check_override_canonicalization(void) {
    static BongoCatSettings settings;
    bongo_cat_settings_defaults(&settings);
    settings.behavior_shortcut_count = 4;
    memcpy(settings.behavior_shortcuts[0].id, "motion:1",
        sizeof("motion:1"));
    memcpy(settings.behavior_shortcuts[0].shortcut, "Control+1",
        sizeof("Control+1"));
    memcpy(settings.behavior_shortcuts[0].label, "First", sizeof("First"));
    memcpy(settings.behavior_shortcuts[1].id, "motion:1",
        sizeof("motion:1"));
    memcpy(settings.behavior_shortcuts[1].shortcut, "Control+2",
        sizeof("Control+2"));
    memcpy(settings.behavior_shortcuts[2].id, "empty", sizeof("empty"));
    memcpy(settings.behavior_shortcuts[3].id, "motion:2",
        sizeof("motion:2"));
    memcpy(settings.behavior_shortcuts[3].label, "Second", sizeof("Second"));
    settings.model_label_count = 2;
    memcpy(settings.model_labels[0].id, "model", sizeof("model"));
    memcpy(settings.model_labels[0].label, "Old", sizeof("Old"));
    memcpy(settings.model_labels[1].id, "model", sizeof("model"));
    memcpy(settings.model_labels[1].label, "New", sizeof("New"));
    bongo_cat_settings_validate(&settings);
    CHECK(settings.behavior_shortcut_count == 2);
    CHECK(strcmp(settings.behavior_shortcuts[0].shortcut, "Control+2") == 0);
    CHECK(strcmp(settings.behavior_shortcuts[0].label, "First") == 0);
    CHECK(strcmp(settings.behavior_shortcuts[1].id, "motion:2") == 0);
    CHECK(settings.model_label_count == 1);
    CHECK(strcmp(settings.model_labels[0].label, "New") == 0);
}

static void check_shortcuts(void) {
    static BongoCatSettings settings;
    bongo_cat_settings_defaults(&settings);
    memcpy(settings.shortcuts.toggle_pet_visibility, "Control+B",
        sizeof("Control+B"));
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
    CHECK(strcmp(settings.shortcuts.toggle_pet_visibility, "Control+B") == 0);
    CHECK(!settings.shortcuts.visible_preferences[0]);
    CHECK(settings.behavior_shortcut_count == 1);
    CHECK(strcmp(settings.behavior_shortcuts[0].id, "motion:1") == 0);
    CHECK(strcmp(settings.behavior_shortcuts[0].shortcut, "Control+M") == 0);
    CHECK(bongo_cat_settings_shortcut_conflicts(&settings, "control+b", NULL));
    CHECK(!bongo_cat_settings_shortcut_conflicts(&settings, "control+b",
        settings.shortcuts.toggle_pet_visibility));
}

void test_config(void) {
    check_defaults_and_validation();
    check_shortcuts();
    check_override_canonicalization();
    BongoCatLanguage language;
    CHECK(!strcmp(bongo_cat_language_name(BONGO_CAT_LANG_ZH_HANT),
        "zh-Hant"));
    CHECK(bongo_cat_language_parse("zh-Hant", &language) &&
        language == BONGO_CAT_LANG_ZH_HANT);
    CHECK(bongo_cat_language_parse("zh-TW", &language) &&
        language == BONGO_CAT_LANG_ZH_HANT);
    CHECK(!bongo_cat_language_parse("zh-Hans", &language));
    const uint32_t colors[] = {0x00ff00, 0x0000ff, 0xff0000, 0xff00ff};
    for (int i = 0; i < BONGO_CAT_OBS_BACKGROUND_COLOR_COUNT; ++i)
        CHECK(bongo_cat_obs_background_color_rgb(i) == colors[i]);

    static BongoCatSettings settings;
    static BongoCatSessionState session;
    bongo_cat_settings_defaults(&settings);
    bongo_cat_session_defaults(&session);
    settings.model.max_fps = 30;
    settings.model.multiple_pets = true;
    settings.model.mirror = true;
    settings.model.mouse_centered = false;
    settings.window.pass_through = true;
    settings.window.obs_background = true;
    settings.window.random_expression = true;
    settings.window.random_expression_interval_seconds = 12.0f;
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
    session.window.width = 700;
    session.window.height = 500;
    session.window.content_width = 612;
    session.window.content_height = 354;
    memcpy(session.active_model_id, "model", sizeof("model"));
    CHECK(bongo_cat_session_add_model(&session, "keyboard"));
    CHECK(bongo_cat_session_add_model(&session, "gamepad"));
    session.active_behavior_count = 2;
    memcpy(session.active_behaviors[0].model_id, "model",
        sizeof("model"));
    memcpy(session.active_behaviors[0].behavior_id,
        "model:motion:Tap:0", sizeof("model:motion:Tap:0"));
    memcpy(session.active_behaviors[1].model_id, "model",
        sizeof("model"));
    memcpy(session.active_behaviors[1].behavior_id,
        "model:expression:2", sizeof("model:expression:2"));

    const char *settings_path = "bongocat-settings.json";
    const char *session_path = "bongocat-session.json";
    BongoCatError error = {0};
    CHECK(bongo_cat_settings_save(settings_path, &settings, &error) ==
        BONGO_CAT_OK);
    CHECK(bongo_cat_session_save(session_path, &session, &error) ==
        BONGO_CAT_OK);
    CHECK(contains_text(settings_path, "\"format\": \"bongocat/settings\""));
    CHECK(contains_text(settings_path, "\"captureBackground\": true"));
    CHECK(contains_text(settings_path, "\"randomExpression\": true"));
    CHECK(contains_text(settings_path,
        "\"randomExpressionIntervalSeconds\": 12.0"));
    CHECK(contains_text(settings_path, "\"multiplePets\": true") && !contains_text(settings_path, "inputReleaseDelaySeconds"));
    CHECK(contains_text(settings_path, "\"example\""));
    CHECK(!contains_text(settings_path, "activeModelId"));
    CHECK(contains_text(session_path, "\"format\": \"bongocat/session\""));
    CHECK(contains_text(session_path, "\"contentWidth\": 612") &&
        contains_text(session_path, "\"contentHeight\": 354"));
    CHECK(contains_text(session_path, "\"activeModelId\": \"model\""));
    CHECK(contains_text(session_path, "\"additionalModelIds\""));
    CHECK(contains_text(session_path, "\"activeBehaviors\""));
    CHECK(contains_text(session_path,
        "\"behaviorId\": \"model:expression:2\""));
    CHECK(!contains_text(session_path, "clickThrough"));

    static BongoCatSettings loaded_settings;
    static BongoCatSessionState loaded_session;
    bongo_cat_settings_defaults(&loaded_settings);
    bongo_cat_session_defaults(&loaded_session);
    CHECK(bongo_cat_settings_load(settings_path, &loaded_settings, &error) ==
        BONGO_CAT_OK);
    CHECK(bongo_cat_session_load(session_path, &loaded_session, &error) ==
        BONGO_CAT_OK);
    CHECK(loaded_settings.model.max_fps == 30 && loaded_settings.model.mirror &&
        loaded_settings.model.multiple_pets);
    CHECK(loaded_settings.window.pass_through &&
        loaded_settings.window.obs_background &&
        loaded_settings.window.random_expression &&
        loaded_settings.window.random_expression_interval_seconds == 12.0f);
    CHECK(loaded_settings.app.language == BONGO_CAT_LANG_ZH_CN);
    CHECK(strstr(loaded_settings.extensions_json,
        "\"enabled\":true") != NULL);
    CHECK(strcmp(bongo_cat_model_name(&loaded_settings,
        &(BongoCatModelEntry){.id = "model", .display_name = "Imported"}),
        "Display") == 0);
    CHECK(loaded_session.window.x == -321 &&
        loaded_session.window.opacity_percent == 75.0f &&
        loaded_session.window.width == 700 &&
        loaded_session.window.height == 500 &&
        loaded_session.window.content_width == 612 &&
        loaded_session.window.content_height == 354);
    CHECK(strcmp(loaded_session.active_model_id, "model") == 0);
    CHECK(loaded_session.additional_model_count == 2 &&
        bongo_cat_session_model_active(&loaded_session, "keyboard") &&
        bongo_cat_session_model_active(&loaded_session, "gamepad"));
    CHECK(loaded_session.active_behavior_count == 2);
    CHECK(strcmp(loaded_session.active_behaviors[0].behavior_id,
        "model:motion:Tap:0") == 0);
    CHECK(strcmp(loaded_session.active_behaviors[1].behavior_id,
        "model:expression:2") == 0);

    const char *unsupported = "bongocat-unsupported.json";
    write_text(unsupported,
        "{\"format\":\"bongocat/settings\",\"schemaVersion\":2}");
    CHECK(bongo_cat_settings_load(unsupported, &loaded_settings, &error) ==
        BONGO_CAT_ERROR_FORMAT);
    write_text(unsupported,
        "{\"format\":\"bongocat/session\",\"schemaVersion\":2}");
    CHECK(bongo_cat_session_load(unsupported, &loaded_session, &error) ==
        BONGO_CAT_ERROR_FORMAT);

    write_text(unsupported, "{\"format\":\"bongocat/settings\",\"schemaVersion\":1,\"rendering\":{\"inputReleaseDelaySeconds\":3,\"maximumFps\":30}}");
    CHECK(bongo_cat_settings_load(unsupported, &loaded_settings, &error) == BONGO_CAT_OK && loaded_settings.model.max_fps == 30);

    write_text(unsupported,
        "{\"format\":\"bongocat/settings\",\"schemaVersion\":1,"
        "\"rendering\":{\"maximumFps\":\"fast\"}}");
    loaded_settings.model.max_fps = 73;
    CHECK(bongo_cat_settings_load(unsupported, &loaded_settings, &error) ==
        BONGO_CAT_ERROR_FORMAT);
    CHECK(loaded_settings.model.max_fps == 73);
    write_text(unsupported,
        "{\"format\":\"bongocat/settings\",\"schemaVersion\":1,"
        "\"rendering\":{\"maximumFps\":1.5}}");
    CHECK(bongo_cat_settings_load(unsupported, &loaded_settings, &error) ==
        BONGO_CAT_ERROR_FORMAT);
    write_text(unsupported,
        "{\"format\":\"bongocat/settings\",\"schemaVersion\":1,"
        "\"rendering\":{\"maximumFps\":30,\"maximumFps\":60}}");
    CHECK(bongo_cat_settings_load(unsupported, &loaded_settings, &error) ==
        BONGO_CAT_ERROR_FORMAT);
    write_text(unsupported,
        "{\"format\":\"bongocat/session\",\"schemaVersion\":1,"
        "\"window\":{\"visible\":123}}");
    CHECK(bongo_cat_session_load(unsupported, &loaded_session, &error) ==
        BONGO_CAT_ERROR_FORMAT);
    write_text(unsupported,
        "{\"format\":\"bongocat/session\",\"schemaVersion\":1,"
        "\"window\":{\"size\":{\"width\":700,\"height\":500}}}");
    bongo_cat_session_defaults(&loaded_session);
    CHECK(bongo_cat_session_load(unsupported, &loaded_session, &error) ==
        BONGO_CAT_OK && loaded_session.window.content_width == 700 &&
        loaded_session.window.content_height == 500);

    static BongoCatSettings canonical_settings;
    static BongoCatSessionState canonical_session;
    bongo_cat_settings_defaults(&canonical_settings);
    bongo_cat_session_defaults(&canonical_session);
    canonical_settings.model.max_fps = 900;
    canonical_session.window.scale_percent = -1.0f;
    CHECK(bongo_cat_settings_save(settings_path, &canonical_settings, &error) ==
        BONGO_CAT_OK);
    CHECK(bongo_cat_session_save(session_path, &canonical_session, &error) ==
        BONGO_CAT_OK);
    bongo_cat_settings_defaults(&loaded_settings);
    bongo_cat_session_defaults(&loaded_session);
    CHECK(bongo_cat_settings_load(settings_path, &loaded_settings, &error) ==
        BONGO_CAT_OK);
    CHECK(bongo_cat_session_load(session_path, &loaded_session, &error) ==
        BONGO_CAT_OK);
    CHECK(loaded_settings.model.max_fps == 240);
    CHECK(loaded_session.window.scale_percent == 10.0f);

    CHECK(bongo_cat_file_remove(settings_path));
    CHECK(bongo_cat_file_remove(session_path));
    CHECK(bongo_cat_file_remove(unsupported));
}
