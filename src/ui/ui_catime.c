#include "ui_catime.h"
#include "ui_backend.h"
#include "ui_animation.h"

#include <math.h>

static struct nk_color rgb(int value) {
    return nk_rgba((value >> 16) & 255, (value >> 8) & 255, value & 255, 255);
}

static float text_width(const struct nk_user_font *font, const char *text) {
    return font && text ? font->width(font->userdata, font->height,
        text, nk_strlen(text)) : 0.0f;
}

static void centered(struct nk_command_buffer *canvas, struct nk_rect bounds,
    const char *text, const struct nk_user_font *font, struct nk_color color) {
    float width = text_width(font, text);
    struct nk_rect target = nk_rect(bounds.x + (bounds.w - width) * .5f,
        bounds.y + (bounds.h - font->height) * .5f, width + 1, font->height);
    nk_draw_text(canvas, target, text, nk_strlen(text), font,
        nk_rgba(0, 0, 0, 0), color);
}

float bongo_cat_neo_ui_sidebar_width(float window_width) {
    return window_width <= 780.0f ? BONGO_CAT_NEO_UI_SIDEBAR_NARROW :
        BONGO_CAT_NEO_UI_SIDEBAR_WIDTH;
}

void bongo_cat_neo_ui_shell_draw(struct nk_context *context, float width,
    float height, bool dark) {
    BongoCatNeoUIPalette p = bongo_cat_neo_ui_palette(dark);
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    float side = bongo_cat_neo_ui_sidebar_width(width);
    struct nk_rect surface = nk_rect(8, 8, width - 16, height - 16);
    nk_fill_rect(canvas, nk_rect(9, 12, width - 18, height - 17), 20,
        dark ? rgb(0x0D0E11) : rgb(0xDDE8F2));
    nk_fill_rect(canvas, surface, 20, p.surface);
    nk_stroke_rect(canvas, surface, 20, 1, p.border);
    nk_fill_rect(canvas, nk_rect(8, 8, side, height - 16), 20,
        dark ? rgb(0x1C2027) : rgb(0xFFFFFF));
    nk_fill_rect(canvas, nk_rect(8 + side - 20, 8, 20, height - 16), 0,
        dark ? rgb(0x1C2027) : rgb(0xFFFFFF));
    nk_stroke_line(canvas, 8 + side, 8, 8 + side, height - 8, 1,
        dark ? rgb(0x343A45) : rgb(0xE3E8EF));
}

static void signature(struct nk_command_buffer *canvas, struct nk_rect bounds,
    BongoCatNeoUIPalette p) {
    float x = bounds.x + (bounds.w - 88) * .5f, y = bounds.y + 132;
    nk_stroke_curve(canvas, x, y, x + 20, y + 4, x + 58, y + 4,
        x + 88, y, 5, p.selection);
    nk_stroke_curve(canvas, x, y - 1, x + 22, y + 2, x + 59, y + 3,
        x + 88, y - 2, 3, p.pink);
}

bool bongo_cat_neo_ui_header(struct nk_context *context, const char *title,
    const struct nk_user_font *font, unsigned int logo_texture,
    bool *title_clicked, bool interactive, bool dark) {
    struct nk_rect bounds;
    float height = nk_window_get_content_region(context).w < 100 ? 118.0f : 148.0f;
    nk_layout_row_dynamic(context, height, 1);
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return false;
    BongoCatNeoUIPalette p = bongo_cat_neo_ui_palette(dark);
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    float size = bounds.w < 100 ? 54.0f : 72.0f;
    struct nk_rect frame = nk_rect(bounds.x + (bounds.w - size) * .5f,
        bounds.y + (bounds.w < 100 ? 10.0f : 13.0f), size, size);
    bool hover = interactive && nk_input_is_mouse_hovering_rect(&context->input,
        nk_rect(bounds.x, bounds.y, bounds.w, bounds.h));
    nk_fill_rect(canvas, frame, bounds.w < 100 ? 14.0f : 18.0f,
        hover ? p.pink_hover : p.accent);
    struct nk_rect inner = nk_rect(frame.x + 3, frame.y + 3,
        frame.w - 6, frame.h - 6);
    nk_fill_rect(canvas, inner, bounds.w < 100 ? 11.0f : 15.0f, p.surface);
    if (logo_texture) {
        struct nk_image image = nk_image_id((int)logo_texture);
        float image_size = bounds.w < 100 ? 42.0f : 56.0f;
        struct nk_rect image_bounds = nk_rect(frame.x + (size - image_size) * .5f,
            frame.y + (size - image_size) * .5f, image_size, image_size);
        nk_draw_image(canvas, image_bounds, &image, nk_rgb(255, 255, 255));
    }
    if (bounds.w >= 100) {
        if (!font) font = bongo_cat_neo_ui_caption_font(context);
        centered(canvas, nk_rect(bounds.x, bounds.y + 88, bounds.w, 28), title,
            font, p.pink);
        signature(canvas, bounds, p);
    }
    if (hover) bongo_cat_neo_ui_cursor_hover_rect(context, bounds,
        BONGO_CAT_NEO_UI_CURSOR_POINTER);
    if (title_clicked) *title_clicked = hover &&
        nk_input_is_mouse_click_in_rect(&context->input, NK_BUTTON_LEFT, bounds);
    return false;
}

static void nav_icon(struct nk_command_buffer *canvas, int index,
    struct nk_rect bounds, struct nk_color color) {
    float x = bounds.x + bounds.w * .5f, y = bounds.y + 17;
    if (index == 0) {
        nk_stroke_circle(canvas, nk_rect(x - 9, y - 7, 18, 16), 2, color);
        nk_stroke_triangle(canvas, x - 9, y - 3, x - 7, y - 11,
            x - 2, y - 6, 2, color);
        nk_stroke_triangle(canvas, x + 2, y - 6, x + 7, y - 11,
            x + 9, y - 3, 2, color);
    } else if (index == 1) {
        nk_stroke_circle(canvas, nk_rect(x - 9, y - 9, 18, 18), 5, color);
        nk_fill_circle(canvas, nk_rect(x - 3, y - 3, 6, 6), color);
    } else if (index == 2) {
        nk_stroke_line(canvas, x - 8, y + 8, x + 7, y - 7, 3, color);
        nk_stroke_line(canvas, x - 7, y - 7, x + 7, y + 7, 2, color);
        nk_fill_circle(canvas, nk_rect(x + 7, y - 11, 4, 4), color);
    } else if (index == 3) {
        nk_stroke_rect(canvas, nk_rect(x - 10, y - 7, 20, 14), 3, 2, color);
        for (int row = 0; row < 2; ++row)
            for (int col = 0; col < 4; ++col)
                nk_fill_rect(canvas, nk_rect(x - 7 + col * 4, y - 4 + row * 5,
                    2, 2), 0, color);
    } else {
        nk_fill_circle(canvas, nk_rect(x - 9, y - 8, 11, 13), color);
        nk_fill_circle(canvas, nk_rect(x - 1, y - 8, 11, 13), color);
        nk_fill_triangle(canvas, x - 8, y - 1, x + 9, y - 1, x, y + 10, color);
    }
}

void bongo_cat_neo_ui_tabs(struct nk_context *context, const char *const *labels,
    int count, int *active, bool interactive, bool dark) {
    BongoCatNeoUIPalette p = bongo_cat_neo_ui_palette(dark);
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    const struct nk_user_font *font = bongo_cat_neo_ui_caption_font(context);
    float nav_position = bongo_cat_neo_ui_animate(context,
        "sidebar-selection", (float)*active, 200.0f);
    for (int i = 0; i < count; ++i) {
        struct nk_rect bounds;
        nk_layout_row_dynamic(context, 68, 1);
        if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) continue;
        bool hover = interactive &&
            nk_input_is_mouse_hovering_rect(&context->input, bounds);
        bool selected = *active == i;
        struct nk_rect tile = nk_rect(bounds.x + 10, bounds.y + 1,
            bounds.w - 20, bounds.h - 1);
        tile.y += 16.0f + i * 8.0f;
        if (hover) nk_fill_rect(canvas, tile, 8, p.hover);
        float weight = NK_MAX(0.0f, 1.0f - fabsf(nav_position - i));
        if (weight > 0.0f) nk_fill_rect(canvas, tile, 8,
            nk_rgba(p.pink.r, p.pink.g, p.pink.b, (nk_byte)(255 * weight)));
        struct nk_color color = selected ? nk_rgb(255, 255, 255) :
            (hover ? p.accent : p.muted);
        nav_icon(canvas, i, tile, color);
        centered(canvas, nk_rect(tile.x, tile.y + 35, tile.w, 28), labels[i],
            font, color);
        if (hover) bongo_cat_neo_ui_cursor_hover_rect(context, tile,
            BONGO_CAT_NEO_UI_CURSOR_POINTER);
        if (hover && nk_input_is_mouse_click_in_rect(&context->input,
            NK_BUTTON_LEFT, tile)) {
            *active = i;
            bongo_cat_neo_ui_animate(context, "sidebar-selection",
                (float)*active, 200.0f);
        }
    }
}

bool bongo_cat_neo_ui_content_header(struct nk_context *context,
    const char *title, int icon, bool interactive, bool dark) {
    struct nk_rect bounds;
    nk_layout_row_dynamic(context, BONGO_CAT_NEO_UI_HEADER_HEIGHT, 1);
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return false;
    BongoCatNeoUIPalette p = bongo_cat_neo_ui_palette(dark);
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    struct nk_rect icon_bounds = nk_rect(bounds.x + 20, bounds.y + 16, 22, 22);
    nav_icon(canvas, icon, icon_bounds, p.accent);
    const struct nk_user_font *font = bongo_cat_neo_ui_label_font(context);
    struct nk_rect text = nk_rect(bounds.x + 52,
        bounds.y + (bounds.h - font->height) * .5f, bounds.w - 110, font->height);
    nk_draw_text(canvas, text, title, nk_strlen(title), font,
        nk_rgba(0, 0, 0, 0), p.accent);
    struct nk_rect close = nk_rect(bounds.x + bounds.w - 48, bounds.y + 11, 34, 34);
    bool hover = interactive && nk_input_is_mouse_hovering_rect(&context->input, close);
    struct nk_color color = hover ? p.accent : p.muted;
    nk_stroke_line(canvas, close.x + 10, close.y + 10,
        close.x + 24, close.y + 24, 2, color);
    nk_stroke_line(canvas, close.x + 24, close.y + 10,
        close.x + 10, close.y + 24, 2, color);
    if (hover) bongo_cat_neo_ui_cursor_hover_rect(context, close,
        BONGO_CAT_NEO_UI_CURSOR_POINTER);
    return hover && nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, close);
}

bool bongo_cat_neo_ui_close_hit(float x, float y, float width) {
    return x >= width - 58.0f && x <= width - 12.0f && y >= 14.0f && y <= 62.0f;
}

bool bongo_cat_neo_ui_title_link_hit(float x, float y, float width) {
    return x >= 8.0f && x <= 8.0f + bongo_cat_neo_ui_sidebar_width(width) &&
        y >= 8.0f && y <= 156.0f;
}

bool bongo_cat_neo_ui_title_drag_hit(float x, float y, float width) {
    float side = bongo_cat_neo_ui_sidebar_width(width);
    return x >= 8.0f + side && x <= width - 8.0f &&
        y >= 8.0f && y <= 8.0f + BONGO_CAT_NEO_UI_HEADER_HEIGHT &&
        !bongo_cat_neo_ui_close_hit(x, y, width);
}
