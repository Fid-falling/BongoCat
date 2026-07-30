#include "ui_backend.h"

#include <stdlib.h>

static float valid_scale(float value) {
    return value >= 0.5f && value <= 4.0f ? value : 1.0f;
}

static float test_scale(void) {
    const char *text = SDL_getenv("BONGO_CAT_TEST_UI_SCALE");
    if (!text || !text[0]) return 0.0f;
    char *end = NULL;
    float value = strtof(text, &end);
    return end && !*end && value >= 0.5f && value <= 4.0f ? value : 0.0f;
}

float bongo_cat_ui_display_layout_scale(SDL_DisplayID display) {
    float override = test_scale();
    if (override > 0.0f) return override;
    return valid_scale(display ? SDL_GetDisplayContentScale(display) : 1.0f);
}

void bongo_cat_ui_query_window_scale(SDL_Window *window,
    float *layout_scale, float *raster_scale) {
    float override = test_scale();
    if (override > 0.0f) {
        if (layout_scale) *layout_scale = override;
        if (raster_scale) *raster_scale = override;
        return;
    }
    float raster = window ? SDL_GetWindowDisplayScale(window) : 1.0f;
    float density = window ? SDL_GetWindowPixelDensity(window) : 1.0f;
    raster = valid_scale(raster);
    density = valid_scale(density);
    if (layout_scale) *layout_scale = valid_scale(raster / density);
    if (raster_scale) *raster_scale = raster;
}

void bongo_cat_ui_logical_size(const BongoCatUIBackend *ui,
    float *width, float *height) {
    int window_width = 1, window_height = 1;
    if (ui && ui->window)
        SDL_GetWindowSize(ui->window, &window_width, &window_height);
    float scale = ui && ui->layout_scale > 0.0f ? ui->layout_scale : 1.0f;
    if (width) *width = SDL_max(1.0f, window_width / scale);
    if (height) *height = SDL_max(1.0f, window_height / scale);
}
