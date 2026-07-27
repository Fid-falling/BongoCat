#ifndef BONGO_CAT_NEO_APP_H
#define BONGO_CAT_NEO_APP_H

#include "bongo_cat_neo/config.h"
#include "bongo_cat_neo/input.h"
#include "bongo_cat_neo/model.h"
#include "bongo_cat_neo/mouse.h"
#include "bongo_cat_neo/platform.h"
#include "bongo_cat_neo/shortcut.h"

typedef struct BongoCatNeoAudio BongoCatNeoAudio;
typedef struct BongoCatNeoTray BongoCatNeoTray;
typedef struct BongoCatNeoOverlay BongoCatNeoOverlay;
typedef struct BongoCatNeoPreferences BongoCatNeoPreferences;
typedef struct BongoCatNeoI18n BongoCatNeoI18n;

typedef struct BongoCatNeoApp {
    BongoCatNeoConfig config;
    BongoCatNeoInputState input;
    BongoCatNeoShortcutState shortcut_state;
    BongoCatNeoModelCatalog models;
    BongoCatNeoBehaviorCatalog behaviors;
    BongoCatNeoI18n *i18n;
    BongoCatNeoPlatform platform;
    BongoCatNeoLive2D *live2d;
    BongoCatNeoAudio *audio;
    BongoCatNeoTray *tray;
    BongoCatNeoOverlay *overlay;
    BongoCatNeoPreferences *preferences;
    SDL_Window *window;
    void *gl_context;
    char preferences_path[BONGO_CAT_NEO_PATH_CAP];
    char session_path[BONGO_CAT_NEO_PATH_CAP];
    char data_root[BONGO_CAT_NEO_PATH_CAP];
    char asset_root[BONGO_CAT_NEO_PATH_CAP];
    char locale_root[BONGO_CAT_NEO_PATH_CAP];
    char smoke_import_path[BONGO_CAT_NEO_PATH_CAP];
    char smoke_model[BONGO_CAT_NEO_ID_CAP];
    char smoke_runtime_model[BONGO_CAT_NEO_ID_CAP];
    char smoke_live2d_scenario[BONGO_CAT_NEO_ID_CAP];
    char loaded_model[BONGO_CAT_NEO_ID_CAP];
    bool running;
    bool preferences_store_valid;
    bool session_store_valid;
    bool autostart_launch;
    bool smoke;
    bool smoke_preferences;
    bool smoke_remove_imported;
    bool smoke_shortcuts;
    bool smoke_menu;
    bool smoke_input_audit;
    bool smoke_ignore_global_input;
    bool smoke_taskbar_visible;
    bool smoke_context_menu;
    bool smoke_frame_audited;
    bool smoke_frame_series;
    bool smoke_runtime_flow;
    unsigned smoke_runtime_stage;
    uint64_t smoke_runtime_flow_ns;
    int smoke_language;
    int smoke_theme;
    int smoke_preference_page;
    int exit_code;
    bool dirty;
    uint64_t last_frame_ns;
    uint64_t smoke_deadline_ns;
    uint64_t hover_deadline_ns;
    uint64_t pointer_hit_deadline_ns;
    uint64_t mouse_last_ns;
    uint64_t frame_audit_bmp_ns;
    uint64_t preferences_saved_hash, preferences_observed_hash;
    uint64_t session_saved_hash, session_observed_hash;
    uint64_t preferences_save_due_ns, session_save_due_ns;
    BongoCatNeoMouseTracking mouse_tracking;
    bool hover_inside;
    bool hover_hidden;
    bool pointer_known;
    bool pointer_hit_dirty;
    bool pointer_transparent;
    bool click_through_valid;
    bool click_through_applied;
    bool left_mouse_down;
    bool right_mouse_down;
    double pointer_x, pointer_y;
    bool resize_gesture;
    float resize_scale_start, resize_scale_target;
    int resize_base_width, resize_base_height;
    bool resize_pending;
    bool wheel_animation_active;
    bool wheel_gesture_active;
    int resize_pixel_width, resize_pixel_height;
    uint64_t wheel_animation_ns, wheel_input_ns, wheel_event_ns;
    float wheel_opacity_target, wheel_scale_target;
    float wheel_center_x, wheel_center_y;
    float wheel_geometry_scale;
    int wheel_base_width, wheel_base_height;
    bool drag_candidate;
    float drag_start_x, drag_start_y;
    float left_stick_x, left_stick_y;
    float right_stick_x, right_stick_y;
    bool left_stick_pressed, right_stick_pressed;
    uint32_t active_gamepad;
} BongoCatNeoApp;

int bongo_cat_neo_app_run(int argc, char **argv);
void bongo_cat_neo_app_apply_input(BongoCatNeoApp *app, const BongoCatNeoInputEvent *event);
void bongo_cat_neo_app_reset_gamepad(BongoCatNeoApp *app);
void bongo_cat_neo_gamepad_event(BongoCatNeoApp *app, const void *sdl_event);
void bongo_cat_neo_app_shortcuts(BongoCatNeoApp *app, const BongoCatNeoInputEvent *event);
bool bongo_cat_neo_app_select_model(BongoCatNeoApp *app, const char *id);
bool bongo_cat_neo_app_run_behavior(BongoCatNeoApp *app,
    const BongoCatNeoBehaviorEntry *behavior);
BongoCatNeoResult bongo_cat_neo_app_import_model(BongoCatNeoApp *app, const char *source, BongoCatNeoError *error);
BongoCatNeoResult bongo_cat_neo_app_remove_model(BongoCatNeoApp *app, const char *id, BongoCatNeoError *error);
void bongo_cat_neo_app_rescan_models(BongoCatNeoApp *app);
void bongo_cat_neo_config_store_flush(BongoCatNeoApp *app);

#endif
