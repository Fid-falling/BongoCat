#ifndef BONGO_CAT_NEO_RUNTIME_INTERNAL_H
#define BONGO_CAT_NEO_RUNTIME_INTERNAL_H

#include "bongo_cat_neo/app.h"
#include <SDL3/SDL.h>

#ifdef BONGO_CAT_NEO_HAS_CUBISM
#define BONGO_CAT_NEO_FRAME_WAIT(app) (1000 / (app)->config.model.max_fps)
#else
#define BONGO_CAT_NEO_FRAME_WAIT(app) 100
#endif

BongoCatNeoResult bongo_cat_neo_window_create(BongoCatNeoApp *app, BongoCatNeoError *error);
void bongo_cat_neo_app_locate_assets(BongoCatNeoApp *app);
void bongo_cat_neo_window_destroy(BongoCatNeoApp *app);
void bongo_cat_neo_window_apply(BongoCatNeoApp *app);
bool bongo_cat_neo_window_event(BongoCatNeoApp *app, const SDL_Event *event);
bool bongo_cat_neo_window_visible_at_pointer(BongoCatNeoApp *app, float x, float y);
void bongo_cat_neo_window_mark_hit_dirty(BongoCatNeoApp *app);
void bongo_cat_neo_window_schedule_pointer_hit(BongoCatNeoApp *app);
void bongo_cat_neo_window_schedule_hit_check(BongoCatNeoApp *app);
int bongo_cat_neo_window_wait_timeout(const BongoCatNeoApp *app, uint64_t now);
bool bongo_cat_neo_window_wait_timeout_self_test(void);
void bongo_cat_neo_window_sync_click_through(BongoCatNeoApp *app);
void bongo_cat_neo_window_apply_pending_resize(BongoCatNeoApp *app);
void bongo_cat_neo_window_wheel(BongoCatNeoApp *app, const SDL_MouseWheelEvent *event);
void bongo_cat_neo_window_update_wheel_animation(BongoCatNeoApp *app, uint64_t now);
void bongo_cat_neo_window_cancel_wheel_animation(BongoCatNeoApp *app);
bool bongo_cat_neo_window_wheel_self_test(BongoCatNeoApp *app);
bool bongo_cat_neo_window_scaled_size(int base_width, int base_height, float base_scale,
    float requested_scale, float *actual_scale, int *width, int *height);
bool bongo_cat_neo_window_apply_geometry(BongoCatNeoApp *app, int x, int y,
    float scale, int width, int height);
bool bongo_cat_neo_window_set_scale(BongoCatNeoApp *app, float scale);
void bongo_cat_neo_window_clamp_to_display(BongoCatNeoApp *app);
void bongo_cat_neo_window_resize_by_pointer(BongoCatNeoApp *app, const SDL_Event *event);
const char *bongo_cat_neo_gamepad_axis_name(Uint8 axis);
const char *bongo_cat_neo_gamepad_button_name(Uint8 button);
void bongo_cat_neo_gamepads_set_enabled(BongoCatNeoApp *app, bool enabled);
void bongo_cat_neo_app_reset_gamepad(BongoCatNeoApp *app);
void bongo_cat_neo_app_apply_mouse(BongoCatNeoApp *app);
void bongo_cat_neo_app_apply_mouse_position(BongoCatNeoApp *app, double x, double y,
    float elapsed_seconds);
void bongo_cat_neo_app_track_hover(BongoCatNeoApp *app, double x, double y);
void bongo_cat_neo_app_update_hover(BongoCatNeoApp *app, uint64_t now);
BongoCatNeoResult bongo_cat_neo_copy_directory(const char *source, const char *target,
    BongoCatNeoError *error);
bool bongo_cat_neo_app_shortcuts_self_test(BongoCatNeoApp *app);
void bongo_cat_neo_window_menu_action(BongoCatNeoApp *app, BongoCatNeoMenuAction action);
bool bongo_cat_neo_window_menu_self_test(BongoCatNeoApp *app);
bool bongo_cat_neo_window_geometry_self_test(BongoCatNeoApp *app);
void bongo_cat_neo_window_show_context_menu(BongoCatNeoApp *app);
void bongo_cat_neo_live2d_audit_run(BongoCatNeoApp *app);
void bongo_cat_neo_frame_audit(BongoCatNeoApp *app, int width, int height);
void bongo_cat_neo_app_render_now(BongoCatNeoApp *app);
void bongo_cat_neo_runtime_flow_update(BongoCatNeoApp *app, uint64_t now);

#endif
