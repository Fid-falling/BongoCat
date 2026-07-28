#include "ui_paint_cache.h"

#include <SDL3/SDL_opengl.h>
#include <stddef.h>
#include <string.h>

struct BongoCatNeoUIPaintTexture {
    BongoCatNeoUIBackend *backend;
    BongoCatNeoUIPaintKey key;
    GLuint texture;
    uint64_t used;
};

static BongoCatNeoUIPaintTexture textures[128];
static uint64_t use_counter;

BongoCatNeoUIPaintTexture *bongo_cat_neo_ui_paint_cache_get(
    BongoCatNeoUIBackend *backend, const BongoCatNeoUIPaintKey *key) {
    BongoCatNeoUIPaintTexture *empty = NULL, *oldest = NULL;
    for (size_t i = 0; i < sizeof(textures) / sizeof(textures[0]); ++i) {
        BongoCatNeoUIPaintTexture *item = &textures[i];
        if (item->texture && item->backend == backend &&
            memcmp(&item->key, key, sizeof(*key)) == 0) {
            item->used = ++use_counter;
            return item;
        }
        if (!item->texture && !empty) empty = item;
        if (item->backend == backend && (!oldest || item->used < oldest->used))
            oldest = item;
    }
    BongoCatNeoUIPaintTexture *item = empty ? empty : oldest;
    if (!item) return NULL;
    if (item->texture) glDeleteTextures(1, &item->texture);
    memset(item, 0, sizeof(*item));
    item->backend = backend;
    item->key = *key;
    item->used = ++use_counter;
    return item;
}

bool bongo_cat_neo_ui_paint_cache_ready(
    const BongoCatNeoUIPaintTexture *item) {
    return item && item->texture != 0;
}

bool bongo_cat_neo_ui_paint_cache_upload(BongoCatNeoUIPaintTexture *item,
    const unsigned char *pixels, bool single_channel) {
    glGenTextures(1, &item->texture);
    glBindTexture(GL_TEXTURE_2D, item->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (single_channel) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_ONE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_ONE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_ONE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, single_channel ? GL_R8 : GL_RGBA8,
        item->key.width, item->key.height, 0,
        single_channel ? GL_RED : GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    return item->texture != 0;
}

void bongo_cat_neo_ui_paint_cache_draw(struct nk_context *context,
    struct nk_rect bounds, const BongoCatNeoUIPaintTexture *item,
    struct nk_color tint) {
    struct nk_image image = nk_image_id((int)item->texture);
    nk_draw_image(nk_window_get_canvas(context), bounds, &image, tint);
}

void bongo_cat_neo_ui_paint_cache_destroy(BongoCatNeoUIBackend *backend) {
    for (size_t i = 0; i < sizeof(textures) / sizeof(textures[0]); ++i) {
        BongoCatNeoUIPaintTexture *item = &textures[i];
        if (item->backend != backend) continue;
        if (item->texture) glDeleteTextures(1, &item->texture);
        memset(item, 0, sizeof(*item));
    }
}
