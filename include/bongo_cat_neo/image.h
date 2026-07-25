#ifndef BONGO_CAT_NEO_IMAGE_H
#define BONGO_CAT_NEO_IMAGE_H

#include "bongo_cat_neo/common.h"

typedef struct SDL_Surface SDL_Surface;

typedef struct BongoCatNeoImage {
    unsigned char *pixels;
    int width;
    int height;
    SDL_Surface *surface;
    bool pixels_stbi;
} BongoCatNeoImage;

#ifdef __cplusplus
extern "C" {
#endif

BongoCatNeoResult bongo_cat_neo_image_load(const char *path, BongoCatNeoImage *image, BongoCatNeoError *error);
void bongo_cat_neo_image_free(BongoCatNeoImage *image);
unsigned int bongo_cat_neo_image_texture(const char *path, int *width, int *height,
    BongoCatNeoError *error);
unsigned int bongo_cat_neo_image_texture_thumbnail(const char *path, int max_width,
    int max_height, int *width, int *height, BongoCatNeoError *error);
unsigned int bongo_cat_neo_image_texture_mipmapped(const char *path, bool direct_decode,
    int *width, int *height, BongoCatNeoError *error);
unsigned int bongo_cat_neo_image_composite_texture(const char *base, const char *left,
    const char *right, unsigned int texture, bool erase_left, bool erase_right,
    BongoCatNeoError *error);

#ifdef __cplusplus
}
#endif

#endif
