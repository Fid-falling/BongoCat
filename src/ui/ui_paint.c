#include "ui_paint.h"
#include "ui_backend.h"

#include <SDL3/SDL_opengl.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { PAINT_GRADIENT = 1, PAINT_RADIAL = 2, PAINT_SHADOW = 3,
    PAINT_RADIAL_CIRCLE = 4 };

typedef struct PaintKey {
    int kind, width, height, radius;
    int first_parameter, second_parameter;
    uint32_t first_color, second_color;
} PaintKey;

typedef struct PaintTexture {
    BongoCatNeoUIBackend *backend;
    PaintKey key;
    GLuint texture;
    uint64_t used;
} PaintTexture;

static PaintTexture textures[128];
static uint64_t use_counter;

static float clamp01(float value) {
    return NK_CLAMP(0.0f, value, 1.0f);
}

struct nk_color bongo_cat_neo_ui_color_mix(struct nk_color first,
    struct nk_color second, float amount) {
    amount = clamp01(amount);
    return nk_rgba((nk_byte)(first.r + (second.r - first.r) * amount + .5f),
        (nk_byte)(first.g + (second.g - first.g) * amount + .5f),
        (nk_byte)(first.b + (second.b - first.b) * amount + .5f),
        (nk_byte)(first.a + (second.a - first.a) * amount + .5f));
}

struct nk_color bongo_cat_neo_ui_color_alpha(struct nk_color color,
    float amount) {
    color.a = (nk_byte)(color.a * clamp01(amount) + .5f);
    return color;
}

static uint32_t pack(struct nk_color color) {
    return (uint32_t)color.r | (uint32_t)color.g << 8 |
        (uint32_t)color.b << 16 | (uint32_t)color.a << 24;
}

static void pixel(unsigned char *target, struct nk_color color, float alpha) {
    target[0] = color.r; target[1] = color.g; target[2] = color.b;
    target[3] = (unsigned char)(color.a * clamp01(alpha) + .5f);
}

static float rounded_distance(float x, float y, float width, float height,
    float radius) {
    radius = NK_CLAMP(0.0f, radius, NK_MIN(width, height) * .5f);
    float qx = fabsf(x - width * .5f) - (width * .5f - radius);
    float qy = fabsf(y - height * .5f) - (height * .5f - radius);
    float outside_x = NK_MAX(qx, 0.0f), outside_y = NK_MAX(qy, 0.0f);
    return sqrtf(outside_x * outside_x + outside_y * outside_y) +
        NK_MIN(NK_MAX(qx, qy), 0.0f) - radius;
}

static bool texture_dimensions(struct nk_context *context,
    struct nk_rect bounds, int *width, int *height, float *scale_x,
    float *scale_y, BongoCatNeoUIBackend **backend) {
    *backend = bongo_cat_neo_ui_backend_for_context(context);
    if (!*backend || !(*backend)->window) return false;
    int logical_width, logical_height, pixel_width, pixel_height;
    SDL_GetWindowSize((*backend)->window, &logical_width, &logical_height);
    SDL_GetWindowSizeInPixels((*backend)->window, &pixel_width, &pixel_height);
    if (logical_width < 1 || logical_height < 1) return false;
    *scale_x = (float)pixel_width / logical_width;
    *scale_y = (float)pixel_height / logical_height;
    *width = NK_MAX(1, (int)ceilf(bounds.w * *scale_x));
    *height = NK_MAX(1, (int)ceilf(bounds.h * *scale_y));
    return true;
}

static PaintTexture *cached(BongoCatNeoUIBackend *backend,
    const PaintKey *key) {
    PaintTexture *empty = NULL, *oldest = NULL;
    for (size_t i = 0; i < sizeof(textures) / sizeof(textures[0]); ++i) {
        PaintTexture *item = &textures[i];
        if (item->texture && item->backend == backend &&
            memcmp(&item->key, key, sizeof(*key)) == 0) {
            item->used = ++use_counter; return item;
        }
        if (!item->texture && !empty) empty = item;
        if (item->backend == backend && (!oldest || item->used < oldest->used))
            oldest = item;
    }
    PaintTexture *item = empty ? empty : oldest;
    if (!item) return NULL;
    if (item->texture) glDeleteTextures(1, &item->texture);
    memset(item, 0, sizeof(*item)); item->backend = backend; item->key = *key;
    item->used = ++use_counter; return item;
}

static bool upload(PaintTexture *item, const unsigned char *pixels) {
    glGenTextures(1, &item->texture);
    glBindTexture(GL_TEXTURE_2D, item->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, item->key.width,
        item->key.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    return item->texture != 0;
}

static void draw_texture(struct nk_context *context, struct nk_rect bounds,
    PaintTexture *item) {
    struct nk_image image = nk_image_id((int)item->texture);
    nk_draw_image(nk_window_get_canvas(context), bounds, &image,
        nk_rgb(255, 255, 255));
}

void bongo_cat_neo_ui_paint_gradient(struct nk_context *context,
    struct nk_rect bounds, float rounding, struct nk_color first,
    struct nk_color second) {
    int width, height; float sx, sy; BongoCatNeoUIBackend *backend;
    if (!texture_dimensions(context, bounds, &width, &height, &sx, &sy,
        &backend)) {
        nk_fill_rect(nk_window_get_canvas(context), bounds, rounding,
            bongo_cat_neo_ui_color_mix(first, second, .5f)); return;
    }
    PaintKey key = {PAINT_GRADIENT, width, height,
        (int)lroundf(rounding * (sx + sy) * .5f), 0, 0,
        pack(first), pack(second)};
    PaintTexture *item = cached(backend, &key); if (!item) return;
    if (!item->texture) {
        size_t bytes = (size_t)width * height * 4;
        unsigned char *pixels = malloc(bytes); if (!pixels) return;
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            float amount = ((x + .5f) / width + (y + .5f) / height) * .5f;
            float distance = rounded_distance(x + .5f, y + .5f,
                (float)width, (float)height, (float)key.radius);
            pixel(pixels + ((size_t)y * width + x) * 4,
                bongo_cat_neo_ui_color_mix(first, second, amount), .5f - distance);
        }
        if (!upload(item, pixels)) { free(pixels); return; } free(pixels);
    }
    draw_texture(context, bounds, item);
}

static void paint_radial(struct nk_context *context, int kind,
    struct nk_rect bounds, struct nk_color center, struct nk_color edge,
    float midpoint, float outer) {
    int width, height; float sx, sy; BongoCatNeoUIBackend *backend;
    if (!texture_dimensions(context, bounds, &width, &height, &sx, &sy,
        &backend)) return;
    PaintKey key = {kind, width, height, 0,
        (int)lroundf(midpoint * 1000), (int)lroundf(outer * 1000),
        pack(center), pack(edge)};
    PaintTexture *item = cached(backend, &key); if (!item) return;
    if (!item->texture) {
        unsigned char *pixels = malloc((size_t)width * height * 4);
        if (!pixels) return;
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            float dx = (2.0f * (x + .5f) - width) / width;
            float dy = (2.0f * (y + .5f) - height) /
                (kind == PAINT_RADIAL_CIRCLE ? width : height);
            float distance = sqrtf(dx * dx + dy * dy);
            float mix = midpoint > 0 ? clamp01(distance / midpoint) : 1.0f;
            float fade = outer > midpoint ?
                clamp01((outer - distance) / (outer - midpoint)) : 0.0f;
            fade = fade * fade * (3.0f - 2.0f * fade);
            struct nk_color color = bongo_cat_neo_ui_color_mix(center, edge, mix);
            pixel(pixels + ((size_t)y * width + x) * 4, color,
                distance <= midpoint ? 1.0f : fade);
        }
        if (!upload(item, pixels)) { free(pixels); return; } free(pixels);
    }
    draw_texture(context, bounds, item);
}

void bongo_cat_neo_ui_paint_radial(struct nk_context *context,
    struct nk_rect bounds, struct nk_color center, struct nk_color edge,
    float midpoint, float outer) {
    paint_radial(context, PAINT_RADIAL, bounds, center, edge, midpoint, outer);
}

void bongo_cat_neo_ui_paint_radial_circle(struct nk_context *context,
    struct nk_rect bounds, struct nk_color center, struct nk_color edge,
    float midpoint, float outer) {
    paint_radial(context, PAINT_RADIAL_CIRCLE, bounds, center, edge,
        midpoint, outer);
}

void bongo_cat_neo_ui_paint_shadow(struct nk_context *context,
    struct nk_rect bounds, float rounding, float offset_x, float offset_y,
    float blur, float spread, struct nk_color color) {
    float pad = ceilf(blur * 1.5f + spread + 2.0f);
    struct nk_rect target = nk_rect(bounds.x + offset_x - pad,
        bounds.y + offset_y - pad, bounds.w + pad * 2, bounds.h + pad * 2);
    int width, height; float sx, sy; BongoCatNeoUIBackend *backend;
    if (!texture_dimensions(context, target, &width, &height, &sx, &sy,
        &backend)) return;
    float scale = (sx + sy) * .5f;
    PaintKey key = {PAINT_SHADOW, width, height,
        (int)lroundf(rounding * scale), (int)lroundf(blur * scale),
        (int)lroundf(spread * scale), pack(color), 0};
    PaintTexture *item = cached(backend, &key); if (!item) return;
    if (!item->texture) {
        unsigned char *pixels = malloc((size_t)width * height * 4);
        if (!pixels) return;
        float pad_x = pad * sx, pad_y = pad * sy;
        float shape_w = bounds.w * sx + 2.0f * key.second_parameter;
        float shape_h = bounds.h * sy + 2.0f * key.second_parameter;
        float center_x = pad_x + bounds.w * sx * .5f;
        float center_y = pad_y + bounds.h * sy * .5f;
        float sigma = NK_MAX(1.0f, (float)key.first_parameter * .5f);
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            float local_x = x + .5f - center_x + shape_w * .5f;
            float local_y = y + .5f - center_y + shape_h * .5f;
            float distance = rounded_distance(local_x, local_y,
                shape_w, shape_h,
                (float)(key.radius + key.second_parameter));
            float alpha = .55f *
                expf(-.5f * distance * distance / (sigma * sigma));
            pixel(pixels + ((size_t)y * width + x) * 4, color, alpha);
        }
        if (!upload(item, pixels)) { free(pixels); return; } free(pixels);
    }
    draw_texture(context, target, item);
}

void bongo_cat_neo_ui_paint_destroy(BongoCatNeoUIBackend *backend) {
    for (size_t i = 0; i < sizeof(textures) / sizeof(textures[0]); ++i) {
        PaintTexture *item = &textures[i];
        if (item->backend != backend) continue;
        if (item->texture) glDeleteTextures(1, &item->texture);
        memset(item, 0, sizeof(*item));
    }
}
