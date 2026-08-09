#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "ui_font_atlas.h"
#include "bongo_cat/file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <sys/mman.h>
#endif

typedef struct UIFontSource {
    void *data;
    size_t size;
    FILE *file;
    void *mapping;
} UIFontSource;

static bool source_load(UIFontSource *source, const char *path) {
    source->file = bongo_cat_file_open(path, "rb");
    if (!source->file || fseek(source->file, 0, SEEK_END) != 0) return false;
    long size = ftell(source->file);
    if (size <= 0 || fseek(source->file, 0, SEEK_SET) != 0) return false;
#ifdef _WIN32
    HANDLE file = (HANDLE)_get_osfhandle(_fileno(source->file));
    HANDLE mapping = CreateFileMappingW(file, NULL, PAGE_READONLY, 0, 0, NULL);
    source->data = mapping ? MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0) : NULL;
    source->mapping = mapping;
    source->size = source->data ? (size_t)size : 0;
#else
    source->data = mmap(NULL, (size_t)size, PROT_READ, MAP_PRIVATE,
        fileno(source->file), 0);
    if (source->data == MAP_FAILED) source->data = NULL;
    source->size = source->data ? (size_t)size : 0;
#endif
    return source->data != NULL;
}

static void source_release(UIFontSource *source) {
#ifdef _WIN32
    if (source->data) UnmapViewOfFile(source->data);
    if (source->mapping) CloseHandle((HANDLE)source->mapping);
#else
    if (source->data) munmap(source->data, source->size);
#endif
    if (source->file) fclose(source->file);
    memset(source, 0, sizeof(*source));
}

static struct nk_font_config font_config(float size, const nk_rune *ranges) {
    struct nk_font_config config = nk_font_config(size);
    config.range = ranges;
    config.oversample_h = 2;
    config.oversample_v = 2;
    return config;
}

static struct nk_font *add_font(struct nk_font_atlas *atlas,
    const UIFontSource *source, float size, const nk_rune *ranges, bool merge) {
    struct nk_font_config config = font_config(size, ranges);
    config.merge_mode = merge;
    int before = atlas->font_num;
    struct nk_font *font;
    if (source && source->data) {
        config.ttf_blob = source->data;
        config.ttf_size = source->size;
        config.ttf_data_owned_by_atlas = 1;
        font = nk_font_atlas_add(atlas, &config);
    } else font = nk_font_atlas_add_default(atlas, size, &config);
    return merge && atlas->font_num > before ? atlas->fonts : font;
}

static void detach_source(struct nk_font_atlas *atlas,
    const UIFontSource *source) {
    if (!source || !source->data) return;
    for (struct nk_font_config *base = atlas->config; base; base = base->next) {
        struct nk_font_config *config = base;
        do {
            if (config->ttf_blob == source->data) config->ttf_blob = NULL;
            config = config->n;
        } while (config != base);
    }
}

static void font_to_front(struct nk_font_atlas *atlas, struct nk_font *font) {
    if (!atlas || !font || atlas->fonts == font) return;
    struct nk_font *previous = atlas->fonts;
    while (previous && previous->next != font) previous = previous->next;
    if (!previous) return;
    previous->next = font->next;
    font->next = atlas->fonts;
    atlas->fonts = font;
}

typedef enum GlyphRangeKind {
    GLYPH_RANGE_PRIMARY,
    GLYPH_RANGE_CJK,
    GLYPH_RANGE_KOREAN
} GlyphRangeKind;

// Nuklear does not skip missing glyphs while merging fonts, so each script
// must be assigned to a font that actually contains it.
static GlyphRangeKind glyph_range_kind(nk_rune point) {
    bool korean = (point >= 0x1100 && point <= 0x11ff) ||
        (point >= 0x3130 && point <= 0x318f) ||
        (point >= 0xa960 && point <= 0xa97f) ||
        (point >= 0xac00 && point <= 0xd7ff);
    if (korean) return GLYPH_RANGE_KOREAN;
    return point >= 0x2e80 ? GLYPH_RANGE_CJK : GLYPH_RANGE_PRIMARY;
}

static void append_range(nk_rune *ranges, size_t *count,
    nk_rune first, nk_rune last) {
    ranges[(*count)++] = first;
    ranges[(*count)++] = last;
}

static int compare_ranges(const void *left, const void *right) {
    const nk_rune *a = left, *b = right;
    return a[0] < b[0] ? -1 : a[0] > b[0] ? 1 :
        (a[1] < b[1] ? -1 : a[1] > b[1]);
}

static void normalize_ranges(nk_rune *ranges, size_t *count) {
    qsort(ranges, *count / 2, sizeof(*ranges) * 2, compare_ranges);
    size_t written = 0;
    for (size_t read = 0; read < *count; read += 2) {
        nk_rune first = ranges[read], last = ranges[read + 1];
        if (written >= 2 && first <= ranges[written - 1] + 1) {
            ranges[written - 1] = NK_MAX(ranges[written - 1], last);
            continue;
        }
        ranges[written++] = first;
        ranges[written++] = last;
    }
    ranges[written] = 0;
    *count = written;
}

static bool split_ranges(const nk_rune *ranges, nk_rune **primary,
    nk_rune **cjk, nk_rune **korean) {
    *primary = NULL; *cjk = NULL; *korean = NULL;
    if (!ranges) return true;
    size_t entries = 0;
    while (ranges[entries]) entries++;
    size_t capacity = entries + 32;
    *primary = calloc(capacity, sizeof(**primary));
    *cjk = calloc(capacity, sizeof(**cjk));
    *korean = calloc(capacity, sizeof(**korean));
    if (!*primary || !*cjk || !*korean) {
        free(*korean); free(*cjk); free(*primary);
        return false;
    }
    static const nk_rune boundaries[] = {0x1100, 0x1200, 0x2e80,
        0x3130, 0x3190, 0xa960, 0xa980, 0xac00, 0xd800};
    size_t counts[3] = {0};
    for (size_t pair = 0; ranges[pair] && ranges[pair + 1]; pair += 2) {
        nk_rune cursor = ranges[pair], last = ranges[pair + 1];
        while (cursor <= last) {
            nk_rune segment_last = last;
            for (size_t i = 0; i < sizeof(boundaries) / sizeof(boundaries[0]); ++i)
                if (boundaries[i] > cursor && boundaries[i] - 1 < segment_last)
                    segment_last = boundaries[i] - 1;
            GlyphRangeKind kind = glyph_range_kind(cursor);
            nk_rune *target = kind == GLYPH_RANGE_PRIMARY ? *primary :
                (kind == GLYPH_RANGE_CJK ? *cjk : *korean);
            append_range(target, &counts[kind], cursor, segment_last);
            if (segment_last == last) break;
            cursor = segment_last + 1;
        }
    }
    normalize_ranges(*primary, &counts[GLYPH_RANGE_PRIMARY]);
    normalize_ranges(*cjk, &counts[GLYPH_RANGE_CJK]);
    normalize_ranges(*korean, &counts[GLYPH_RANGE_KOREAN]);
    return true;
}

static struct nk_font *add_family_font(struct nk_font_atlas *atlas,
    const UIFontSource *primary, const UIFontSource *fallback,
    const UIFontSource *korean_fallback, float size, const nk_rune *all,
    const nk_rune *primary_ranges, const nk_rune *cjk,
    const nk_rune *korean) {
    bool merge = primary && primary->data && fallback && fallback->data &&
        ((cjk && cjk[0]) || (korean && korean[0]));
    const UIFontSource *base = primary && primary->data ? primary : fallback;
    struct nk_font *font = add_font(atlas, base, size,
        merge ? primary_ranges : all, false);
    if (!font || !merge) return font;
    font_to_front(atlas, font);
    if (cjk && cjk[0] && !add_font(atlas, fallback, size, cjk, true))
        return NULL;
    const UIFontSource *korean_source = korean_fallback &&
        korean_fallback->data ? korean_fallback : fallback;
    if (korean && korean[0] &&
        !add_font(atlas, korean_source, size, korean, true)) return NULL;
    return font;
}

static bool font_has_ranges(const struct nk_font *font, const nk_rune *ranges) {
    if (!font) return false;
    if (!ranges) {
        const struct nk_font_glyph *glyph = nk_font_find_glyph(font, 'A');
        return glyph && glyph->codepoint == 'A';
    }
    for (size_t pair = 2; ranges[pair] && ranges[pair + 1]; pair += 2)
        for (nk_rune point = ranges[pair]; point <= ranges[pair + 1]; ++point) {
            const struct nk_font_glyph *glyph = nk_font_find_glyph(font, point);
            if (!glyph || glyph->codepoint != point) return false;
        }
    return true;
}

static bool upload_atlas(BongoCatUIBackend *ui) {
    int width = 0, height = 0;
    const void *pixels = nk_font_atlas_bake(&ui->atlas, &width, &height,
        NK_FONT_ATLAS_ALPHA8);
    if (!pixels || width < 1 || height < 1) return false;
    GLint maximum = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
    if (width > maximum || height > maximum) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Preferences font atlas %dx%d exceeds GPU limit %d", width, height,
            maximum);
        return false;
    }
    ui->font_atlas_width = width;
    ui->font_atlas_height = height;
    SDL_Log("Preferences font atlas ready: %dx%d", width, height);
    bongo_cat_gl_clear_errors();
    glGenTextures(1, &ui->font_texture);
    glBindTexture(GL_TEXTURE_2D, ui->font_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_ONE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_ONE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_ONE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED,
        GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    GLenum upload_error = glGetError();
    if (!ui->font_texture || upload_error != GL_NO_ERROR) {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
            "Preferences font atlas upload failed (0x%x)",
            (unsigned)upload_error);
        if (ui->font_texture) glDeleteTextures(1, &ui->font_texture);
        ui->font_texture = 0;
        ui->font_atlas_width = 0;
        ui->font_atlas_height = 0;
        return false;
    }
    nk_font_atlas_end(&ui->atlas, nk_handle_id((int)ui->font_texture),
        &ui->null_texture);
    return true;
}

bool bongo_cat_ui_font_atlas_create(BongoCatUIBackend *ui,
    const char *body_path, const char *body_fallback_path,
    const char *body_korean_fallback_path, const char *heading_path,
    const char *heading_fallback_path,
    const char *heading_korean_fallback_path,
    const nk_rune *glyph_ranges, float raster_scale) {
    UIFontSource body = {0}, body_fallback = {0}, body_korean_fallback = {0};
    UIFontSource heading = {0}, heading_fallback = {0};
    UIFontSource heading_korean_fallback = {0};
    bool body_loaded = body_path && source_load(&body, body_path);
    bool body_fallback_loaded = body_fallback_path &&
        (!body_path || strcmp(body_path, body_fallback_path) != 0) &&
        source_load(&body_fallback, body_fallback_path);
    bool body_korean_fallback_loaded = body_korean_fallback_path &&
        (!body_path || strcmp(body_path, body_korean_fallback_path) != 0) &&
        (!body_fallback_path || strcmp(body_fallback_path,
            body_korean_fallback_path) != 0) &&
        source_load(&body_korean_fallback, body_korean_fallback_path);
    bool heading_loaded = heading_path && source_load(&heading, heading_path);
    bool heading_fallback_loaded = heading_fallback_path &&
        (!heading_path || strcmp(heading_path, heading_fallback_path) != 0) &&
        source_load(&heading_fallback, heading_fallback_path);
    bool heading_korean_fallback_loaded = heading_korean_fallback_path &&
        (!heading_path || strcmp(heading_path,
            heading_korean_fallback_path) != 0) &&
        (!heading_fallback_path || strcmp(heading_fallback_path,
            heading_korean_fallback_path) != 0) &&
        source_load(&heading_korean_fallback,
            heading_korean_fallback_path);
    const UIFontSource *heading_source = heading_loaded ? &heading :
        (body_loaded ? &body : NULL);
    const UIFontSource *heading_fallback_source = heading_fallback_loaded ?
        &heading_fallback : (body_fallback_loaded ? &body_fallback : NULL);
    const UIFontSource *heading_korean_fallback_source =
        heading_korean_fallback_loaded ? &heading_korean_fallback :
        (body_korean_fallback_loaded ? &body_korean_fallback :
        heading_fallback_source);
    nk_rune *primary_ranges = NULL, *cjk_ranges = NULL;
    nk_rune *korean_ranges = NULL;
    if (!split_ranges(glyph_ranges, &primary_ranges, &cjk_ranges,
        &korean_ranges)) {
        source_release(&heading_korean_fallback);
        source_release(&heading_fallback); source_release(&heading);
        source_release(&body_korean_fallback);
        source_release(&body_fallback); source_release(&body);
        return false;
    }
    nk_font_atlas_init_default(&ui->atlas);
    nk_font_atlas_begin(&ui->atlas);
    GLint maximum_texture = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum_texture);
    float scale_limit = maximum_texture >= 16384 ? 2.0f :
        (maximum_texture >= 8192 ? 1.25f : 1.0f);
    float font_scale = NK_CLAMP(1.0f, raster_scale, scale_limit);
    if (font_scale + .01f < raster_scale) SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
        "Preferences font raster scale limited to %.2fx by GPU texture size %d",
        font_scale, maximum_texture);
    const UIFontSource *body_source = body_loaded ? &body : NULL;
    const UIFontSource *fallback_source = body_fallback_loaded ? &body_fallback : NULL;
    const UIFontSource *body_korean_fallback_source =
        body_korean_fallback_loaded ? &body_korean_fallback : fallback_source;
    struct nk_font *caption_font = add_family_font(&ui->atlas, body_source,
        fallback_source, body_korean_fallback_source, 18.0f * font_scale,
        glyph_ranges, primary_ranges, cjk_ranges, korean_ranges);
    struct nk_font *body_font = add_family_font(&ui->atlas, body_source,
        fallback_source, body_korean_fallback_source, 20.0f * font_scale,
        glyph_ranges, primary_ranges, cjk_ranges, korean_ranges);
    struct nk_font *label_font = add_family_font(&ui->atlas, heading_source,
        heading_fallback_source, heading_korean_fallback_source,
        20.0f * font_scale, glyph_ranges, primary_ranges, cjk_ranges,
        korean_ranges);
    struct nk_font *heading_font = add_family_font(&ui->atlas, heading_source,
        heading_fallback_source, heading_korean_fallback_source,
        28.0f * font_scale, glyph_ranges, primary_ranges, cjk_ranges,
        korean_ranges);
    struct nk_font *hero_font = add_family_font(&ui->atlas, heading_source,
        heading_fallback_source, heading_korean_fallback_source,
        36.0f * font_scale, glyph_ranges, primary_ranges, cjk_ranges,
        korean_ranges);
    bool uploaded = caption_font && body_font && label_font && heading_font &&
        hero_font && upload_atlas(ui);
    if (uploaded) {
        ui->caption_font = &caption_font->handle;
        ui->body_font = &body_font->handle;
        ui->label_font = &label_font->handle;
        ui->heading_font = &heading_font->handle;
        ui->hero_font = &hero_font->handle;
        caption_font->handle.height = 18.0f;
        body_font->handle.height = 20.0f;
        label_font->handle.height = 20.0f;
        heading_font->handle.height = 28.0f;
        hero_font->handle.height = 36.0f;
        ui->latin_glyph_ranges = primary_ranges;
        ui->cjk_glyph_ranges = cjk_ranges;
        ui->korean_glyph_ranges = korean_ranges;
        ui->font_probe_loaded = font_has_ranges(body_font, glyph_ranges);
        nk_style_set_font(&ui->context, ui->body_font);
    } else {
        free(korean_ranges); free(cjk_ranges); free(primary_ranges);
    }
    ui->font_path_found = body_path != NULL || body_fallback_path != NULL ||
        body_korean_fallback_path != NULL;
    ui->font_file_loaded = body_loaded || body_fallback_loaded ||
        body_korean_fallback_loaded;
    ui->custom_font_loaded = ui->font_file_loaded && body_font != NULL;
    detach_source(&ui->atlas, &heading_korean_fallback);
    detach_source(&ui->atlas, &heading_fallback);
    detach_source(&ui->atlas, &heading);
    detach_source(&ui->atlas, &body_korean_fallback);
    detach_source(&ui->atlas, &body_fallback);
    detach_source(&ui->atlas, &body);
    nk_font_atlas_cleanup(&ui->atlas);
    source_release(&heading_korean_fallback);
    source_release(&heading_fallback);
    source_release(&heading);
    source_release(&body_korean_fallback);
    source_release(&body_fallback);
    source_release(&body);
    return uploaded;
}

void bongo_cat_ui_font_atlas_destroy(BongoCatUIBackend *ui) {
    if (!ui) return;
    nk_font_atlas_clear(&ui->atlas);
    free(ui->korean_glyph_ranges); free(ui->cjk_glyph_ranges);
    free(ui->latin_glyph_ranges);
    ui->korean_glyph_ranges = NULL; ui->cjk_glyph_ranges = NULL;
    ui->latin_glyph_ranges = NULL;
    ui->caption_font = NULL;
    ui->body_font = NULL;
    ui->label_font = NULL;
    ui->heading_font = NULL;
    ui->hero_font = NULL;
    ui->font_atlas_width = 0;
    ui->font_atlas_height = 0;
}
