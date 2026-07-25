#ifndef BONGO_CAT_NEO_PLATFORM_H
#define BONGO_CAT_NEO_PLATFORM_H

#include "bongo_cat_neo/config.h"
#include "bongo_cat_neo/input.h"

typedef struct SDL_Window SDL_Window;

typedef struct BongoCatNeoPlatform {
    SDL_Window *window;
    BongoCatNeoInputState *input;
    void *native;
} BongoCatNeoPlatform;

typedef enum BongoCatNeoMenuAction {
    BONGO_CAT_NEO_MENU_NONE,
    BONGO_CAT_NEO_MENU_PREFERENCES,
    BONGO_CAT_NEO_MENU_HIDE,
    BONGO_CAT_NEO_MENU_PASS_THROUGH,
    BONGO_CAT_NEO_MENU_ALWAYS_ON_TOP,
    BONGO_CAT_NEO_MENU_SCALE_50,
    BONGO_CAT_NEO_MENU_SCALE_60,
    BONGO_CAT_NEO_MENU_SCALE_70,
    BONGO_CAT_NEO_MENU_SCALE_80,
    BONGO_CAT_NEO_MENU_SCALE_90,
    BONGO_CAT_NEO_MENU_SCALE_100,
    BONGO_CAT_NEO_MENU_SCALE_110,
    BONGO_CAT_NEO_MENU_SCALE_120,
    BONGO_CAT_NEO_MENU_SCALE_130,
    BONGO_CAT_NEO_MENU_SCALE_140,
    BONGO_CAT_NEO_MENU_SCALE_150,
    BONGO_CAT_NEO_MENU_SCALE_160,
    BONGO_CAT_NEO_MENU_SCALE_170,
    BONGO_CAT_NEO_MENU_SCALE_180,
    BONGO_CAT_NEO_MENU_SCALE_190,
    BONGO_CAT_NEO_MENU_SCALE_200,
    BONGO_CAT_NEO_MENU_OPACITY_10,
    BONGO_CAT_NEO_MENU_OPACITY_20,
    BONGO_CAT_NEO_MENU_OPACITY_30,
    BONGO_CAT_NEO_MENU_OPACITY_40,
    BONGO_CAT_NEO_MENU_OPACITY_50,
    BONGO_CAT_NEO_MENU_OPACITY_60,
    BONGO_CAT_NEO_MENU_OPACITY_70,
    BONGO_CAT_NEO_MENU_OPACITY_80,
    BONGO_CAT_NEO_MENU_OPACITY_90,
    BONGO_CAT_NEO_MENU_OPACITY_100,
    BONGO_CAT_NEO_MENU_EXIT,
    BONGO_CAT_NEO_MENU_MODEL_FIRST = 1000,
    BONGO_CAT_NEO_MENU_MOTION_FIRST = 2000,
    BONGO_CAT_NEO_MENU_EXPRESSION_FIRST = 3000
} BongoCatNeoMenuAction;
typedef void (*BongoCatNeoMenuPreview)(void *userdata, BongoCatNeoMenuAction action);

typedef struct BongoCatNeoMenuLabels {
    const char *preferences, *hide, *pass_through, *always_on_top;
    const char *window_size, *opacity, *model, *exit;
    const char *wheel_size_hint, *wheel_opacity_hint, *motion, *expression;
    const char *const *model_names, *const *motion_names, *const *expression_names;
    size_t model_count, current_model, motion_count, expression_count;
    float scale_percent, opacity_percent;
    bool pass_through_checked, always_on_top_checked, dark_theme;
    BongoCatNeoMenuPreview preview, restore;
    void *preview_userdata;
} BongoCatNeoMenuLabels;

typedef void (*BongoCatNeoTrayClick)(void *userdata);

BongoCatNeoResult bongo_cat_neo_platform_init(BongoCatNeoPlatform *platform, SDL_Window *window,
    BongoCatNeoInputState *input, BongoCatNeoError *error);
void bongo_cat_neo_platform_shutdown(BongoCatNeoPlatform *platform);
void bongo_cat_neo_platform_set_click_through(BongoCatNeoPlatform *platform, bool enabled);
bool bongo_cat_neo_platform_pointer_local(BongoCatNeoPlatform *platform, double screen_x,
    double screen_y, float *local_x, float *local_y);
void bongo_cat_neo_platform_set_always_on_top(BongoCatNeoPlatform *platform, bool enabled);
void bongo_cat_neo_platform_set_taskbar(BongoCatNeoPlatform *platform, bool visible);
void bongo_cat_neo_platform_raise_window(SDL_Window *window);
bool bongo_cat_neo_platform_set_geometry(BongoCatNeoPlatform *platform,
    int x, int y, int width, int height);
void bongo_cat_neo_platform_begin_drag(BongoCatNeoPlatform *platform);
bool bongo_cat_neo_platform_dynamic_hit_supported(void);
void bongo_cat_neo_platform_set_tray_left_click(void *tray, BongoCatNeoTrayClick callback,
    void *userdata);
bool bongo_cat_neo_platform_single_instance_begin(void);
void bongo_cat_neo_platform_single_instance_end(void);
BongoCatNeoResult bongo_cat_neo_platform_set_autostart(bool enabled, BongoCatNeoError *error);
BongoCatNeoMenuAction bongo_cat_neo_platform_context_menu(BongoCatNeoPlatform *platform,
    const BongoCatNeoMenuLabels *labels);
BongoCatNeoResult bongo_cat_neo_platform_embedded_assets(const char *target, BongoCatNeoError *error);

#endif
