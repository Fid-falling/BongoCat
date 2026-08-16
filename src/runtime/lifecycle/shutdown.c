#include "runtime.h"
#include "bongo_cat/audio.h"
#include "bongo_cat/i18n.h"
#include "bongo_cat/overlay.h"
#include "bongo_cat/preferences.h"
#include "bongo_cat/tray.h"

void bongo_cat_app_shutdown(BongoCatApp *app, const char *stage,
    int exit_code) {
    bongo_cat_runtime_stage(app, stage);
    SDL_Log("[runtime] Shutdown started: stage=%s exit_code=%d",
        stage, exit_code);
    bongo_cat_config_store_flush(app);
    bongo_cat_preferences_destroy(app->preferences);
    bongo_cat_i18n_destroy(app->i18n);
    bongo_cat_tray_destroy(app->tray);
    bongo_cat_gamepads_set_enabled(app, false);
    bongo_cat_audio_destroy(app->audio);
    bongo_cat_overlay_destroy(app->overlay);
    bongo_cat_live2d_destroy(app->live2d);
    bongo_cat_platform_shutdown(&app->platform);
    bongo_cat_runtime_log_stop();
    bongo_cat_window_destroy(app);
    bongo_cat_runtime_clean_shutdown(app, exit_code);
}
