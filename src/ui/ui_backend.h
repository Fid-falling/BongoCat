#ifndef BONGO_CAT_NEO_UI_BACKEND_H
#define BONGO_CAT_NEO_UI_BACKEND_H

#include "bongo_cat_neo/gl_api.h"
#include <SDL3/SDL.h>
#include "nuklear_config.h"

typedef enum BongoCatNeoUICursor {
    BONGO_CAT_NEO_UI_CURSOR_DEFAULT,
    BONGO_CAT_NEO_UI_CURSOR_POINTER,
    BONGO_CAT_NEO_UI_CURSOR_TEXT
} BongoCatNeoUICursor;

typedef struct BongoCatNeoUIBackend {
    SDL_Window *window;
    BongoCatNeoGL gl;
    struct nk_context context;
    struct nk_font_atlas atlas;
    struct nk_buffer commands;
    struct nk_draw_null_texture null_texture;
    GLuint program;
    GLint texture_location;
    GLint projection_location;
    GLuint vao;
    GLuint vbo;
    GLuint ebo;
    GLuint font_texture;
    const struct nk_user_font *caption_font;
    const struct nk_user_font *body_font;
    const struct nk_user_font *label_font;
    const struct nk_user_font *heading_font;
    nk_rune *latin_glyph_ranges;
    nk_rune *cjk_glyph_ranges;
    void *vertices;
    void *elements;
    size_t vertex_capacity;
    size_t element_capacity;
    size_t last_vertex_bytes;
    size_t last_element_bytes;
    unsigned last_draw_commands;
    unsigned last_draw_elements;
    int last_convert_result;
    GLenum last_gl_error;
    unsigned nonzero_alpha_vertices;
    unsigned max_alpha;
    bool custom_font_loaded;
    bool font_probe_loaded;
    bool font_path_found;
    bool font_file_loaded;
    bool dark_theme;
    SDL_Cursor *default_cursor;
    SDL_Cursor *pointer_cursor;
    SDL_Cursor *text_cursor;
    BongoCatNeoUICursor requested_cursor;
} BongoCatNeoUIBackend;

bool bongo_cat_neo_ui_init(BongoCatNeoUIBackend *ui, SDL_Window *window,
    const char *body_font_path, const char *body_fallback_path,
    const char *heading_font_path, const char *heading_fallback_path,
    const nk_rune *glyph_ranges, BongoCatNeoError *error);
void bongo_cat_neo_ui_destroy(BongoCatNeoUIBackend *ui);
void bongo_cat_neo_ui_input_begin(BongoCatNeoUIBackend *ui);
void bongo_cat_neo_ui_input_end(BongoCatNeoUIBackend *ui);
bool bongo_cat_neo_ui_event(BongoCatNeoUIBackend *ui, const SDL_Event *event);
void bongo_cat_neo_ui_render(BongoCatNeoUIBackend *ui);
bool bongo_cat_neo_ui_frame_valid(const BongoCatNeoUIBackend *ui);
BongoCatNeoUIBackend *bongo_cat_neo_ui_backend_for_context(
    const struct nk_context *context);
const struct nk_user_font *bongo_cat_neo_ui_caption_font(
    const struct nk_context *context);
const struct nk_user_font *bongo_cat_neo_ui_body_font(
    const struct nk_context *context);
const struct nk_user_font *bongo_cat_neo_ui_label_font(
    const struct nk_context *context);
const struct nk_user_font *bongo_cat_neo_ui_heading_font(
    const struct nk_context *context);
void bongo_cat_neo_ui_cursor_begin(BongoCatNeoUIBackend *ui);
void bongo_cat_neo_ui_cursor_apply(BongoCatNeoUIBackend *ui);
void bongo_cat_neo_ui_cursor_destroy(BongoCatNeoUIBackend *ui);
void bongo_cat_neo_ui_cursor_reset(struct nk_context *context);
void bongo_cat_neo_ui_cursor_hover_rect(struct nk_context *context,
    struct nk_rect bounds, BongoCatNeoUICursor cursor);
void bongo_cat_neo_ui_cursor_hover_widget(struct nk_context *context,
    BongoCatNeoUICursor cursor);

#endif
