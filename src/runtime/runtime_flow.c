#include "runtime.h"
#include "bongo_cat_neo/file.h"
#include "bongo_cat_neo/path.h"
#include "bongo_cat_neo/preferences.h"

#include <stdio.h>
#include <string.h>

static void mark(BongoCatNeoApp *app, const char *name) {
    char path[BONGO_CAT_NEO_PATH_CAP];
    bongo_cat_neo_path_join(path, sizeof(path), app->data_root, "runtime-flow-stage.txt");
    FILE *file = bongo_cat_neo_file_open(path, "wb");
    if (file) { fputs(name, file); fclose(file); }
}

static void scale(BongoCatNeoApp *app, float value) {
    bongo_cat_neo_window_cancel_wheel_animation(app);
    bongo_cat_neo_window_set_scale(app, value);
}

static void opacity(BongoCatNeoApp *app, float value) {
    app->config.window.opacity_percent = value;
    SDL_SetWindowOpacity(app->window, value / 100.0f);
}

static void model(BongoCatNeoApp *app, size_t index) {
    if (index < app->models.count) bongo_cat_neo_app_select_model(app, app->models.entries[index].id);
}

static void model_mode(BongoCatNeoApp *app, BongoCatNeoModelMode mode) {
    for (size_t i = 0; i < app->models.count; ++i)
        if (app->models.entries[i].mode == mode) { model(app, i); return; }
}

void bongo_cat_neo_runtime_flow_update(BongoCatNeoApp *app, uint64_t now) {
    if (!app || !app->smoke_runtime_flow) return;
    if (!app->smoke_runtime_flow_ns) {
        app->smoke_runtime_flow_ns = now;
        snprintf(app->smoke_runtime_model, sizeof(app->smoke_runtime_model), "%.*s",
            (int)sizeof(app->smoke_runtime_model) - 1, app->config.current_model);
        mark(app, "startup"); return;
    }
    uint64_t elapsed = now - app->smoke_runtime_flow_ns;
    unsigned target = (unsigned)(elapsed / 1500000000ull);
    if (target <= app->smoke_runtime_stage) return;
    app->smoke_runtime_stage = target;
    switch (target) {
    case 1: mark(app, "idle"); break;
    case 2: scale(app, 125.0f); mark(app, "scale-up"); break;
    case 3: scale(app, 75.0f); mark(app, "scale-down"); break;
    case 4: opacity(app, 50.0f); mark(app, "opacity-50"); break;
    case 5: opacity(app, 100.0f); mark(app, "opacity-100"); break;
    case 6: model_mode(app, BONGO_CAT_NEO_MODE_KEYBOARD); mark(app, "model-keyboard"); break;
    case 7: model_mode(app, BONGO_CAT_NEO_MODE_STANDARD); mark(app, "model-standard"); break;
    case 8: model_mode(app, BONGO_CAT_NEO_MODE_GAMEPAD); mark(app, "model-gamepad"); break;
    case 9: bongo_cat_neo_preferences_show(app->preferences); mark(app, "settings-first"); break;
    case 10: mark(app, "settings-idle"); break;
    case 11: bongo_cat_neo_preferences_close(app->preferences); mark(app, "settings-closed"); break;
    case 12: bongo_cat_neo_preferences_show(app->preferences); mark(app, "settings-reopen"); break;
    case 13:
        bongo_cat_neo_preferences_close(app->preferences);
        bongo_cat_neo_app_select_model(app, app->smoke_runtime_model);
        mark(app, "recovery"); break;
    case 14: model_mode(app, BONGO_CAT_NEO_MODE_GAMEPAD); mark(app, "gamepad-repeat"); break;
    case 15:
        bongo_cat_neo_app_select_model(app, app->smoke_runtime_model);
        mark(app, "final-recovery"); break;
    default: app->running = false; break;
    }
}
