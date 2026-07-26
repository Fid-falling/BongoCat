#include "preferences_state.h"
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
}

void bongo_cat_neo_preferences_support_assets_load(BongoCatNeoPreferences *value) {
    if (value->support_assets_loaded) return;
    value->support_assets_loaded = true;
    value->catime_texture = load(value, "catime.png", 192,
        &value->catime_width, &value->catime_height);
    value->vlaina_texture = load(value, "vlaina.jpg", 192,
        &value->vlaina_width, &value->vlaina_height);
}

static void clear(unsigned int *texture) {
    if (*texture) glDeleteTextures(1, texture);
    *texture = 0;
}

void bongo_cat_neo_preferences_assets_clear(BongoCatNeoPreferences *value) {
    clear(&value->logo_texture);
    clear(&value->catime_texture);
    clear(&value->vlaina_texture);
    value->support_assets_loaded = false;
}
