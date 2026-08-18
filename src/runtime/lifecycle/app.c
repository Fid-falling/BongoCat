#include "runtime.h"
#include "bongo_cat/audio.h"
#include "bongo_cat/file.h"
#include "bongo_cat/i18n.h"
#include "bongo_cat/path.h"
#include "bongo_cat/overlay.h"
#include "bongo_cat/preferences.h"
#include "bongo_cat/tray.h"
#include <SDL3/SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void scan_models(BongoCatApp *app) {
    bongo_cat_app_rescan_models(app);
}
static bool load_selected_model(BongoCatApp *app, BongoCatError *error) {
    const char *candidates[] = {app->session.active_model_id, "standard", "keyboard", "gamepad"};
    for (size_t i = 0; i < 4; ++i) {
        bool duplicate = false;
        for (size_t j = 0; j < i; ++j)
            if (strcmp(candidates[i], candidates[j]) == 0) duplicate = true;
        if (!duplicate && bongo_cat_models_find(&app->models, candidates[i]) &&
            bongo_cat_app_select_model(app, candidates[i])) {
            if (i) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Selected model was unavailable; loaded fallback model %s", candidates[i]);
            return true;
        }
    }
    for (size_t i = 0; i < app->models.count; ++i)
        if (bongo_cat_app_select_model(app, app->models.entries[i].id)) return true;
    bongo_cat_error_set(error, BONGO_CAT_ERROR_CUBISM,
        "No usable Live2D model could be loaded");
    return false;
}
bool bongo_cat_app_initialize(BongoCatApp *app, int argc, char **argv,
    BongoCatError *error) {
    memset(app, 0, sizeof(*app));
    if (argc > 0 && argv && argv[0])
        snprintf(app->executable_path, sizeof(app->executable_path),
            "%s", argv[0]);
    app->smoke_language = -1;
    app->smoke_theme = -1;
    app->smoke_preference_page = -1;
    bongo_cat_settings_defaults(&app->settings);
    bongo_cat_session_defaults(&app->session);
    bongo_cat_input_init(&app->input);
    bongo_cat_shortcut_init(&app->shortcut_state);
    bongo_cat_models_init(&app->models);
    if (!bongo_cat_startup_prepare(app, argc, argv, error)) return false;
    bongo_cat_config_store_load(app);
    if (app->secondary_pet) {
        snprintf(app->session.active_model_id,
            sizeof(app->session.active_model_id), "%s",
            app->secondary_model_id);
        bongo_cat_session_clear_additional_models(&app->session);
        if (!app->session_store_valid && app->secondary_origin_known) {
            app->session.window.position_known = true;
            app->session.window.x = app->secondary_origin_x;
            app->session.window.y = app->secondary_origin_y;
        }
        /* The control file reveals the child on its first update. Starting
           hidden prevents a stale child session from flashing on screen. */
        app->session.window.visible = false;
    } else if (!app->settings.model.multiple_pets)
        bongo_cat_session_clear_additional_models(&app->session);
    if (app->smoke_language >= 0)
        app->settings.app.language = (BongoCatLanguage)app->smoke_language;
    if (app->smoke_theme >= 0) app->settings.app.theme = (BongoCatTheme)app->smoke_theme;
    if (app->smoke_pass_through) app->settings.window.pass_through = true;
    if (app->smoke_model[0])
        snprintf(app->session.active_model_id, sizeof(app->session.active_model_id),
            "%s", app->smoke_model);
    if (!app->secondary_pet && !app->autostart_launch)
        app->session.window.visible = true;
    bongo_cat_startup_stage(app, "configuration-ready");
    if (bongo_cat_window_create(app, error) != BONGO_CAT_OK) return false;
    bongo_cat_startup_stage(app, "window-ready");
    if (bongo_cat_app_locate_assets(app, error) != BONGO_CAT_OK) return false;
    bongo_cat_startup_stage(app, "assets-ready");
    BongoCatError optional = {0};
    app->i18n = bongo_cat_i18n_create(app->locale_root, app->settings.app.language, &optional);
    if (!app->i18n) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", optional.message);
    if (bongo_cat_platform_init(&app->platform, app->window, &app->input, error) != BONGO_CAT_OK) return false;
    bongo_cat_window_apply(app);
    bongo_cat_startup_stage(app, "platform-ready");
    app->live2d = bongo_cat_live2d_create(app->asset_root, error);
    if (!app->live2d) return false;
    optional = (BongoCatError){0}; app->overlay = bongo_cat_overlay_create(&optional);
    if (!app->overlay) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "Overlay disabled: %s", optional.message);
    optional = (BongoCatError){0}; app->audio = bongo_cat_audio_create(&optional);
    if (!app->audio) SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "%s", optional.message);
    else bongo_cat_audio_set_enabled(app->audio, true);
    scan_models(app);
    if (!load_selected_model(app, error)) return false;
    bongo_cat_startup_stage(app, "model-ready");
    if (app->smoke_import_path[0]) {
        BongoCatError import_error = {0};
        if (bongo_cat_app_import_model(app, app->smoke_import_path, &import_error) != BONGO_CAT_OK) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", import_error.message);
            bongo_cat_startup_ci_failure(app, &import_error);
        } else if (app->smoke_remove_imported) {
            char imported[BONGO_CAT_PATH_CAP];
            snprintf(imported, sizeof(imported), "%s", app->session.active_model_id);
            if (bongo_cat_app_remove_model(app, imported, &import_error) != BONGO_CAT_OK)
                bongo_cat_startup_ci_failure(app, &import_error);
        }
    }
    app->running = true;
    optional = (BongoCatError){0}; app->tray = app->secondary_pet ? NULL :
        bongo_cat_tray_create(app, &optional);
    if (!app->secondary_pet && !app->tray && app->settings.app.tray_visible)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", optional.message);
    app->preferences = app->secondary_pet ? NULL :
        bongo_cat_preferences_create(app);
    if (!app->secondary_pet && !app->preferences)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "Preferences are unavailable because their state could not be allocated");
    if (!app->secondary_pet && !app->tray && !app->session.window.visible) {
        bongo_cat_window_set_visible(app, true);
    }
    bongo_cat_live2d_audit_run(app);
    if (app->smoke_context_menu) bongo_cat_window_show_context_menu(app);
    if (app->smoke_shortcuts && !bongo_cat_app_shortcuts_self_test(app)) {
        BongoCatError shortcut_error = {0};
        bongo_cat_error_set(&shortcut_error, BONGO_CAT_ERROR_PLATFORM, "Shortcut action self-test failed");
        bongo_cat_startup_ci_failure(app, &shortcut_error);
    }
    if (app->smoke_menu) {
        bool menu = bongo_cat_window_menu_self_test(app);
        bool geometry = bongo_cat_window_geometry_self_test(app);
        bool display = bongo_cat_window_display_self_test(app);
        bool wheel = bongo_cat_window_wheel_self_test(app);
        bool tray = bongo_cat_tray_self_test(app->tray);
        bool wait = bongo_cat_window_wait_timeout_self_test();
        SDL_Log("Menu self-test: menu=%d geometry=%d display=%d wheel=%d tray=%d wait=%d",
            menu, geometry, display, wheel, tray, wait);
        if (!menu || !geometry || !display || !wheel || !tray || !wait) {
            BongoCatError menu_error = {0};
            bongo_cat_error_set(&menu_error, BONGO_CAT_ERROR_PLATFORM,
                "Context menu action self-test failed (menu=%d geometry=%d display=%d wheel=%d tray=%d wait=%d)",
                menu, geometry, display, wheel, tray, wait);
            bongo_cat_startup_ci_failure(app, &menu_error);
        }
    }
    if (app->smoke_preferences) bongo_cat_preferences_show(app->preferences);
    app->last_frame_ns = SDL_GetTicksNS();
    app->dirty = true;
    app->startup_raise_due_ns = !app->secondary_pet && !app->smoke &&
        !app->autostart_launch && app->session.window.visible ?
        app->last_frame_ns + 250000000ull : 0;
    if (app->smoke_deadline_ns) app->smoke_deadline_ns += app->last_frame_ns;
    if (!app->session.window.visible) bongo_cat_startup_ready(app);
    return true;
}
static void handle_event(BongoCatApp *app, const SDL_Event *event) {
    if (bongo_cat_preferences_event(app->preferences, event)) return;
    if (!bongo_cat_window_event(app, event)) app->running = false;
    if (event->type >= SDL_EVENT_GAMEPAD_AXIS_MOTION &&
        event->type <= SDL_EVENT_GAMEPAD_TOUCHPAD_UP) bongo_cat_gamepad_event(app, event);
}
void bongo_cat_app_drain_input(BongoCatApp *app, bool allow_shortcuts) {
    if (app && app->secondary_pet) allow_shortcuts = false;
    BongoCatInputEvent event;
    while (bongo_cat_input_pop(&app->input, &event)) {
        if (app->smoke_ignore_global_input) continue;
        if (app->smoke_input_audit) {
            char path[BONGO_CAT_PATH_CAP];
            bongo_cat_path_join(path, sizeof(path), app->state_root, "input-audit.txt");
            FILE *file = bongo_cat_file_open(path, "ab");
            if (file) { fprintf(file, "kind=%d name=%s value=%.3f\n",
                event.kind, event.name, event.value); fclose(file); }
        }
        uint64_t delay = strcmp(event.name, "CapsLock") == 0 ? 100 : 0;
#ifdef _WIN32
        if (event.kind == BONGO_CAT_INPUT_KEY_DOWN && !delay)
            delay = (uint64_t)(app->settings.model.auto_release_seconds * 1000.0f);
#endif
        bongo_cat_input_auto_release(&app->input, &event, delay);
        if (allow_shortcuts && !bongo_cat_preferences_shortcuts_blocked(app->preferences))
            bongo_cat_app_shortcuts(app, &event);
        bongo_cat_app_apply_input(app, &event);
    }
    uint64_t now = SDL_GetTicks();
    while (bongo_cat_input_take_release(&app->input, now, &event)) {
        if (allow_shortcuts && !bongo_cat_preferences_shortcuts_blocked(app->preferences))
            bongo_cat_app_shortcuts(app, &event);
        bongo_cat_app_apply_input(app, &event);
    }
    if (!app->smoke_ignore_global_input) bongo_cat_app_apply_mouse(app);
}
static void update_model(BongoCatApp *app, uint64_t now) {
    float elapsed = (float)((now - app->last_frame_ns) / 1000000000.0);
    if (elapsed > 0.25f) elapsed = 0.25f;
    app->last_frame_ns = now;
    if (app->smoke_freeze_model) return;
    bongo_cat_app_step_live2d(app, elapsed);
}
static bool render(BongoCatApp *app, bool present) {
    uint64_t now = SDL_GetTicksNS();
    if (app->render_retry_ns > now) return false;
    if (!SDL_GL_MakeCurrent(app->window, app->gl_context)) {
        app->dirty = true;
        app->render_retry_ns = now + 1000000000ull;
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
            "Main GL context could not be activated: %s", SDL_GetError());
        return false;
    }
    app->render_retry_ns = 0;
    int width, height;
    SDL_GetWindowSizeInPixels(app->window, &width, &height);
    glViewport(0, 0, width, height); glEnable(GL_MULTISAMPLE);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    bongo_cat_window_clear_background(app);
    bongo_cat_overlay_draw_background(app->overlay, app->settings.model.mirror);
    bongo_cat_live2d_set_mirror(app->live2d, app->settings.model.mirror);
    bongo_cat_live2d_draw(app->live2d);
    bongo_cat_overlay_draw_pointer_before_keys(app->overlay);
    if (app->settings.model.mouse_centered && app->pointer_known && !app->model_pointer_anchor_ready) bongo_cat_app_apply_mouse_position(app, app->pointer_x, app->pointer_y, 0.0f);
    bongo_cat_overlay_draw_keys(app->overlay, app->settings.model.mirror);
    bongo_cat_overlay_draw_effect(app->overlay, app->settings.model.mirror);
    bongo_cat_overlay_draw_pointer_after_keys(app->overlay);
    bongo_cat_frame_capture_pending(app, width, height);
    if (!present) {
        app->dirty = true;
        return true;
    }
    bongo_cat_frame_audit(app, width, height);
    bongo_cat_window_capture_pointer_hit(app);
    if (!bongo_cat_platform_present(&app->platform, width, height)) {
        app->dirty = true;
        app->render_retry_ns = now + 1000000000ull;
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
            "Main frame presentation failed: %s", SDL_GetError());
        return false;
    }
    bongo_cat_frame_presented_audit(app);
    bongo_cat_startup_ready(app); bongo_cat_memory_policy_frame_presented(); app->dirty = false;
    bongo_cat_window_sync_click_through(app); bongo_cat_window_schedule_hit_check(app);
    return true;
}
void bongo_cat_app_render_now(BongoCatApp *app) { if (app && app->window &&
    app->session.window.visible && !app->window_minimized) render(app, true); }
bool bongo_cat_app_capture_pending_frame(BongoCatApp *app) {
    return app && app->window && app->pending_model_cover_path[0] &&
        !app->window_minimized && render(app, false);
}
static void take_instance_wake(BongoCatApp *app) {
    if (!bongo_cat_platform_single_instance_take_wake()) return;
    bongo_cat_window_set_visible(app, true);
    bongo_cat_platform_raise_window(app->window);
    SDL_Log("Existing instance requested window reveal");
}
void bongo_cat_app_loop(BongoCatApp *app) {
    uint64_t iterations = 0, wakes = 0, zero_waits = 0;
    while (app->running) {
        iterations++;
        int wait_ms = bongo_cat_window_wait_timeout(app, SDL_GetTicksNS());
        if (app->secondary_pet && wait_ms > 100) wait_ms = 100;
        if (!wait_ms) zero_waits++;
        bongo_cat_preferences_input_begin(app->preferences);
        SDL_Event event;
        if (bongo_cat_wait_event(&event, wait_ms)) {
            wakes++;
            handle_event(app, &event);
            unsigned queued = 0;
            while (queued++ < 256 && SDL_PollEvent(&event))
                handle_event(app, &event);
        }
        bongo_cat_preferences_input_end(app->preferences);
        take_instance_wake(app);
        uint64_t now = SDL_GetTicksNS(); bongo_cat_window_update_wheel_animation(app, now);
        bongo_cat_multi_pet_update(app, now);
        bongo_cat_random_expression_update(app, now);
        bongo_cat_window_update_display_recovery(app, now);
        bongo_cat_runtime_flow_update(app, now);
        bongo_cat_window_apply_pending_resize(app);
        bongo_cat_app_update_hover(app, now);
        bongo_cat_app_drain_input(app, true);
        if (bongo_cat_model_frame_due(app, now)) update_model(app, now);
        else if (!app->session.window.visible || app->window_minimized) { app->last_frame_ns = now; bongo_cat_memory_policy_idle(); }
        if (app->session.window.visible && !app->window_minimized && app->dirty) render(app, true);
        bongo_cat_preferences_render(app->preferences);
        bongo_cat_preferences_process_model_selection(app->preferences);
        if (app->session.window.visible && !app->window_minimized && app->dirty) render(app, true);
        bongo_cat_tray_sync(app->tray); bongo_cat_window_raise_when_due(app, now);
        bongo_cat_config_store_update(app, now);
        if (app->smoke_deadline_ns && now >= app->smoke_deadline_ns) app->running = false;
    }
    if (app->smoke) SDL_Log("Smoke loop: iterations=%llu wakes=%llu zero_waits=%llu",
        (unsigned long long)iterations, (unsigned long long)wakes,
        (unsigned long long)zero_waits);
}
