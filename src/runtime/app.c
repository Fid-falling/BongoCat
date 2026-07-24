#include "runtime.h"
#include "bongo_cat_neo/audio.h"
#include "bongo_cat_neo/file.h"
#include "bongo_cat_neo/i18n.h"
#include "bongo_cat_neo/path.h"
#include "bongo_cat_neo/overlay.h"
#include "bongo_cat_neo/preferences.h"
#include "bongo_cat_neo/tray.h"
#include <SDL3/SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void parse_arguments(BongoCatNeoApp *app, int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--ci-smoke") == 0) app->smoke = true;
        else if (strcmp(argv[i], "--ci-preferences") == 0) app->smoke_preferences = true;
        else if (strcmp(argv[i], "--ci-remove-imported") == 0) app->smoke_remove_imported = true;
        else if (strcmp(argv[i], "--ci-shortcuts") == 0) app->smoke_shortcuts = true;
        else if (strcmp(argv[i], "--ci-menu") == 0) app->smoke_menu = true;
        else if (strcmp(argv[i], "--ci-input-audit") == 0) app->smoke_input_audit = true;
        else if (strcmp(argv[i], "--ci-ignore-global-input") == 0)
            app->smoke_ignore_global_input = true;
        else if (strcmp(argv[i], "--ci-taskbar-visible") == 0)
            app->smoke_taskbar_visible = true;
        else if (strcmp(argv[i], "--ci-context-menu") == 0) app->smoke_context_menu = true;
        else if (strcmp(argv[i], "--ci-frame-series") == 0) app->smoke_frame_series = true;
        else if (strcmp(argv[i], "--ci-runtime-flow") == 0) app->smoke_runtime_flow = true;
        else if (strncmp(argv[i], "--ci-preference-page=", 21) == 0) {
            int page = atoi(argv[i] + 21);
            if (page >= 0 && page < 5) app->smoke_preference_page = page;
        }
        else if (strncmp(argv[i], "--ci-language=", 14) == 0) {
            const char *name = argv[i] + 14;
            for (int language = 0; language <= BONGO_CAT_NEO_LANG_VI_VN; ++language)
                if (strcmp(name, bongo_cat_neo_language_name((BongoCatNeoLanguage)language)) == 0)
                    app->smoke_language = language;
        }
        else if (strncmp(argv[i], "--ci-theme=", 11) == 0) {
            const char *name = argv[i] + 11;
            for (int theme = 0; theme <= BONGO_CAT_NEO_THEME_DARK; ++theme)
                if (strcmp(name, bongo_cat_neo_theme_name((BongoCatNeoTheme)theme)) == 0)
                    app->smoke_theme = theme;
        }
        else if (strncmp(argv[i], "--ci-model=", 11) == 0)
            snprintf(app->smoke_model, sizeof(app->smoke_model), "%s", argv[i] + 11);
        else if (strncmp(argv[i], "--ci-live2d-scenario=", 21) == 0)
            snprintf(app->smoke_live2d_scenario, sizeof(app->smoke_live2d_scenario),
                "%s", argv[i] + 21);
        else if (strncmp(argv[i], "--ci-exit-ms=", 13) == 0) {
            uint64_t delay = strtoull(argv[i] + 13, NULL, 10);
            app->smoke_deadline_ns = delay * 1000000ull;
        } else if (strncmp(argv[i], "--config=", 9) == 0) {
            snprintf(app->config_path, sizeof(app->config_path), "%s", argv[i] + 9);
        } else if (strncmp(argv[i], "--data-root=", 12) == 0) {
            snprintf(app->data_root, sizeof(app->data_root), "%s", argv[i] + 12);
        } else if (strncmp(argv[i], "--ci-import=", 12) == 0) {
            snprintf(app->smoke_import_path, sizeof(app->smoke_import_path), "%s", argv[i] + 12);
        }
    }
    if (app->smoke && !app->smoke_deadline_ns) app->smoke_deadline_ns = 1500000000ull;
}
static void locate_config(BongoCatNeoApp *app) {
    if (!app->data_root[0]) {
        char *pref = SDL_GetPrefPath("BongoCatNeo", BONGO_CAT_NEO_NAME);
        if (!pref) return;
        snprintf(app->data_root, sizeof(app->data_root), "%s", pref);
        SDL_free(pref);
    }
    SDL_CreateDirectory(app->data_root);
    if (!app->config_path[0]) bongo_cat_neo_path_join(app->config_path,
        sizeof(app->config_path), app->data_root, "settings.json");
}
static void ci_failure(BongoCatNeoApp *app, const BongoCatNeoError *error) {
    app->exit_code = 1;
    if (!app->data_root[0]) return;
    char path[BONGO_CAT_NEO_PATH_CAP];
    bongo_cat_neo_path_join(path, sizeof(path), app->data_root, "ci-error.log");
    FILE *file = bongo_cat_neo_file_open(path, "wb");
    if (!file) return;
    fputs(error && error->message[0] ? error->message : "CI operation failed", file);
    fclose(file);
}
static void scan_models(BongoCatNeoApp *app) {
    bongo_cat_neo_app_rescan_models(app);
}
static void load_selected_model(BongoCatNeoApp *app) {
    const BongoCatNeoModelEntry *entry = bongo_cat_neo_models_find(&app->models, app->config.current_model);
    if (!entry && app->models.count) entry = &app->models.entries[0];
    if (!entry) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No model3 directory found");
        return;
    }
    bongo_cat_neo_app_select_model(app, entry->id);
}
static bool initialize(BongoCatNeoApp *app, int argc, char **argv, BongoCatNeoError *error) {
    memset(app, 0, sizeof(*app));
    app->smoke_language = -1;
    app->smoke_theme = -1;
    app->smoke_preference_page = -1;
    bongo_cat_neo_config_defaults(&app->config);
    bongo_cat_neo_input_init(&app->input);
    bongo_cat_neo_shortcut_init(&app->shortcut_state);
    bongo_cat_neo_models_init(&app->models);
    parse_arguments(app, argc, argv);
    locate_config(app);
    if (app->config_path[0]) {
        BongoCatNeoResult loaded = bongo_cat_neo_config_load(app->config_path, &app->config, error);
        if (loaded != BONGO_CAT_NEO_OK) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", error->message);
    }
    if (app->smoke_language >= 0)
        app->config.app.language = (BongoCatNeoLanguage)app->smoke_language;
    if (app->smoke_theme >= 0)
        app->config.app.theme = (BongoCatNeoTheme)app->smoke_theme;
    if (app->smoke_taskbar_visible) app->config.window.taskbar_visible = true;
    if (app->smoke_model[0])
        snprintf(app->config.current_model, sizeof(app->config.current_model),
            "%s", app->smoke_model);
    if (bongo_cat_neo_window_create(app, error) != BONGO_CAT_NEO_OK) return false;
    bongo_cat_neo_app_locate_assets(app);
    app->i18n = bongo_cat_neo_i18n_create(app->locale_root, app->config.app.language, error);
    if (!app->i18n) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", error->message);
    if (bongo_cat_neo_platform_init(&app->platform, app->window, &app->input, error) != BONGO_CAT_NEO_OK) return false;
    bongo_cat_neo_window_apply(app);
    app->live2d = bongo_cat_neo_live2d_create(app->asset_root, error);
    if (!app->live2d) return false;
    app->overlay = bongo_cat_neo_overlay_create(error);
    if (!app->overlay) return false;
    app->audio = bongo_cat_neo_audio_create(error);
    if (!app->audio) SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "%s", error->message);
    else bongo_cat_neo_audio_set_enabled(app->audio, app->config.model.motion_sound);
    scan_models(app);
    load_selected_model(app);
    if (app->smoke_import_path[0]) {
        BongoCatNeoError import_error = {0};
        if (bongo_cat_neo_app_import_model(app, app->smoke_import_path, &import_error) != BONGO_CAT_NEO_OK) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", import_error.message);
            ci_failure(app, &import_error);
        } else if (app->smoke_remove_imported) {
            char imported[BONGO_CAT_NEO_PATH_CAP];
            snprintf(imported, sizeof(imported), "%s", app->config.current_model);
            if (bongo_cat_neo_app_remove_model(app, imported, &import_error) != BONGO_CAT_NEO_OK)
                ci_failure(app, &import_error);
        }
    }
    app->running = true;
    app->tray = bongo_cat_neo_tray_create(app, error);
    if (!app->tray && app->config.app.tray_visible)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", error->message);
    app->preferences = bongo_cat_neo_preferences_create(app);
    if (app->smoke_context_menu) bongo_cat_neo_window_show_context_menu(app);
    bongo_cat_neo_live2d_audit_run(app);
    if (app->smoke_shortcuts && !bongo_cat_neo_app_shortcuts_self_test(app)) {
        BongoCatNeoError shortcut_error = {0};
        bongo_cat_neo_error_set(&shortcut_error, BONGO_CAT_NEO_ERROR_PLATFORM, "Shortcut action self-test failed");
        ci_failure(app, &shortcut_error);
    }
    if (app->smoke_menu) {
        bool menu = bongo_cat_neo_window_menu_self_test(app);
        bool geometry = bongo_cat_neo_window_geometry_self_test(app);
        bool wheel = bongo_cat_neo_window_wheel_self_test(app);
        bool tray = bongo_cat_neo_tray_self_test(app->tray);
        bool wait = bongo_cat_neo_window_wait_timeout_self_test();
        if (!menu || !geometry || !wheel || !tray || !wait) {
            BongoCatNeoError menu_error = {0};
            bongo_cat_neo_error_set(&menu_error, BONGO_CAT_NEO_ERROR_PLATFORM,
                "Context menu action self-test failed (menu=%d geometry=%d wheel=%d tray=%d wait=%d)",
                menu, geometry, wheel, tray, wait);
            ci_failure(app, &menu_error);
        }
    }
    if (app->smoke_preferences) bongo_cat_neo_preferences_show(app->preferences);
    app->last_frame_ns = SDL_GetTicksNS();
    app->dirty = true;
    if (app->smoke_deadline_ns) app->smoke_deadline_ns += app->last_frame_ns;
    return true;
}
static void handle_event(BongoCatNeoApp *app, const SDL_Event *event) {
    if (bongo_cat_neo_preferences_event(app->preferences, event)) return;
    if (!bongo_cat_neo_window_event(app, event)) app->running = false;
    if (event->type >= SDL_EVENT_GAMEPAD_AXIS_MOTION &&
        event->type <= SDL_EVENT_GAMEPAD_TOUCHPAD_UP) bongo_cat_neo_gamepad_event(app, event);
}
static void drain_input(BongoCatNeoApp *app) {
    BongoCatNeoInputEvent event;
    while (bongo_cat_neo_input_pop(&app->input, &event)) {
        if (app->smoke_ignore_global_input) continue;
        if (app->smoke_input_audit) {
            char path[BONGO_CAT_NEO_PATH_CAP];
            bongo_cat_neo_path_join(path, sizeof(path), app->data_root, "input-audit.txt");
            FILE *file = bongo_cat_neo_file_open(path, "ab");
            if (file) { fprintf(file, "kind=%d name=%s value=%.3f\n",
                event.kind, event.name, event.value); fclose(file); }
        }
        uint64_t delay = strcmp(event.name, "CapsLock") == 0 ? 100 : 0;
#ifdef _WIN32
        if (event.kind == BONGO_CAT_NEO_INPUT_KEY_DOWN && !delay)
            delay = (uint64_t)(app->config.model.auto_release_seconds * 1000.0f);
#endif
        bongo_cat_neo_input_auto_release(&app->input, &event, delay);
        if (!bongo_cat_neo_preferences_shortcuts_blocked(app->preferences))
            bongo_cat_neo_app_shortcuts(app, &event);
        bongo_cat_neo_app_apply_input(app, &event);
    }
    uint64_t now = SDL_GetTicks();
    while (bongo_cat_neo_input_take_release(&app->input, now, &event)) {
        if (!bongo_cat_neo_preferences_shortcuts_blocked(app->preferences))
            bongo_cat_neo_app_shortcuts(app, &event);
        bongo_cat_neo_app_apply_input(app, &event);
    }
    if (!app->smoke_ignore_global_input) bongo_cat_neo_app_apply_mouse(app);
}
static void update_model(BongoCatNeoApp *app, uint64_t now) {
    float elapsed = (float)((now - app->last_frame_ns) / 1000000000.0);
    if (elapsed > 0.25f) elapsed = 0.25f;
    app->last_frame_ns = now;
    if (bongo_cat_neo_live2d_update(app->live2d, elapsed)) app->dirty = true;
}
static void render(BongoCatNeoApp *app) {
    SDL_GL_MakeCurrent(app->window, app->gl_context);
    int width, height;
    SDL_GetWindowSizeInPixels(app->window, &width, &height);
    glViewport(0, 0, width, height); glEnable(GL_MULTISAMPLE);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    bongo_cat_neo_overlay_draw_background(app->overlay, app->config.model.mirror);
    bongo_cat_neo_live2d_set_mirror(app->live2d, app->config.model.mirror);
    bongo_cat_neo_live2d_draw(app->live2d);
    bongo_cat_neo_overlay_draw_keys(app->overlay, app->config.model.mirror);
    bongo_cat_neo_frame_audit(app, width, height);
    SDL_GL_SwapWindow(app->window);
    app->dirty = false;
    bongo_cat_neo_window_sync_click_through(app); bongo_cat_neo_window_schedule_hit_check(app);
}
void bongo_cat_neo_app_render_now(BongoCatNeoApp *app) { if (app && app->window && app->config.window.visible) render(app); }
static void loop(BongoCatNeoApp *app) {
    while (app->running) {
        int wait_ms = bongo_cat_neo_window_wait_timeout(app, SDL_GetTicksNS());
        bongo_cat_neo_preferences_input_begin(app->preferences);
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, wait_ms)) {
            handle_event(app, &event);
            while (SDL_PollEvent(&event)) handle_event(app, &event);
        }
        bongo_cat_neo_preferences_input_end(app->preferences);
        drain_input(app);
        uint64_t now = SDL_GetTicksNS(); bongo_cat_neo_window_update_wheel_animation(app, now);
        bongo_cat_neo_runtime_flow_update(app, now);
        bongo_cat_neo_window_apply_pending_resize(app);
        bongo_cat_neo_app_update_hover(app, now);
        if (app->config.window.visible) update_model(app, now); else app->last_frame_ns = now;
        if (app->config.window.visible && app->dirty) render(app);
        bongo_cat_neo_preferences_render(app->preferences);
        bongo_cat_neo_tray_sync(app->tray);
        if (app->smoke_deadline_ns && now >= app->smoke_deadline_ns) app->running = false;
    }
}
static void shutdown(BongoCatNeoApp *app) {
    if (!app->smoke && app->config_path[0]) {
        BongoCatNeoError error = {0};
        if (bongo_cat_neo_config_save(app->config_path, &app->config, &error) != BONGO_CAT_NEO_OK)
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
    }
    bongo_cat_neo_preferences_destroy(app->preferences);
    bongo_cat_neo_i18n_destroy(app->i18n); bongo_cat_neo_tray_destroy(app->tray);
    bongo_cat_neo_gamepads_set_enabled(app, false);
    bongo_cat_neo_audio_destroy(app->audio);
    bongo_cat_neo_overlay_destroy(app->overlay);
    bongo_cat_neo_live2d_destroy(app->live2d);
    bongo_cat_neo_platform_shutdown(&app->platform);
    bongo_cat_neo_window_destroy(app);
}

int bongo_cat_neo_app_run(int argc, char **argv) {
    if (!bongo_cat_neo_platform_single_instance_begin()) return 0;
    BongoCatNeoApp *app = calloc(1, sizeof(*app));
    if (!app) { bongo_cat_neo_platform_single_instance_end(); return 1; }
    BongoCatNeoError error = {0};
    if (!initialize(app, argc, argv, &error)) {
        fprintf(stderr, "%s\n", error.message[0] ? error.message : "Initialization failed");
        if (app->smoke) ci_failure(app, &error);
        shutdown(app); free(app);
        bongo_cat_neo_platform_single_instance_end();
        return 1;
    }
    loop(app);
    shutdown(app);
    int exit_code = app->exit_code;
    free(app); bongo_cat_neo_platform_single_instance_end();
    return exit_code;
}
