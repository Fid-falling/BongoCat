#include "preferences_state.h"
#include "ui_icons.h"
#include "bongo_cat_neo/image.h"
#include "bongo_cat_neo/path.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

static unsigned int load(BongoCatNeoPreferences *value, const char *name,
    int size, int *width, int *height) {
    char path[BONGO_CAT_NEO_PATH_CAP];
    if (!bongo_cat_neo_path_join(path, sizeof(path), value->app->asset_root,
        name)) return 0;
    BongoCatNeoError error = {0};
    unsigned int texture = bongo_cat_neo_image_texture_thumbnail(path, size,
        size, width, height, &error);
    if (!texture && error.message[0])
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
    return texture;
}

void bongo_cat_neo_preferences_assets_load(BongoCatNeoPreferences *value) {
    value->logo_texture = load(value, "logo.png", 192,
        &value->logo_width, &value->logo_height);
    int width = 0, height = 0;
    value->icon_texture = load(value, "ui-symbols.png", 0, &width, &height);
    value->icon_texture_hidpi = load(value, "ui-symbols@4x.png", 0,
        &width, &height);
}

void bongo_cat_neo_preferences_icon_draw(const BongoCatNeoPreferences *value,
    struct nk_command_buffer *canvas, int icon, struct nk_rect bounds,
    struct nk_color color) {
    if (!value || !value->icon_texture || icon < 0 ||
        icon >= BONGO_CAT_NEO_UI_ICON_COUNT) return;
    int logical_width = 0, logical_height = 0, pixel_width = 0, pixel_height = 0;
    SDL_GetWindowSize(value->window, &logical_width, &logical_height);
    SDL_GetWindowSizeInPixels(value->window, &pixel_width, &pixel_height);
    bool large = bounds.w > 24.0f || bounds.h > 24.0f;
    bool hidpi = value->icon_texture_hidpi && (large ||
        pixel_width > logical_width * 3 / 2 ||
        pixel_height > logical_height * 3 / 2);
    int cell = hidpi ? 96 : 24;
    int atlas_width = cell * BONGO_CAT_NEO_UI_ICON_COUNT;
    unsigned int texture = hidpi ? value->icon_texture_hidpi : value->icon_texture;
    struct nk_image image = nk_subimage_id((int)texture, (nk_ushort)atlas_width,
        (nk_ushort)cell, nk_recti(icon * cell, 0, cell, cell));
    nk_draw_image(canvas, bounds, &image, color);
}

void bongo_cat_neo_preferences_support_assets_load(BongoCatNeoPreferences *value) {
    if (value->support_assets_loaded) return;
    value->support_assets_loaded = true;
    value->catime_texture = load(value, "catime.png", 192,
        &value->catime_width, &value->catime_height);
    char path[BONGO_CAT_NEO_PATH_CAP];
    if (bongo_cat_neo_path_join(path, sizeof(path), value->app->asset_root,
        "vlaina.jpg")) {
        BongoCatNeoError error = {0};
        value->vlaina_texture = bongo_cat_neo_image_texture_resampled(path,
            192, 192, 48, &value->vlaina_width, &value->vlaina_height, &error);
        if (!value->vlaina_texture && error.message[0])
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
    }
}

static void clear(unsigned int *texture) {
    if (*texture) glDeleteTextures(1, texture);
    *texture = 0;
}

void bongo_cat_neo_preferences_assets_clear(BongoCatNeoPreferences *value) {
    clear(&value->logo_texture);
    clear(&value->icon_texture);
    clear(&value->icon_texture_hidpi);
    clear(&value->catime_texture);
    clear(&value->vlaina_texture);
    value->support_assets_loaded = false;
}
