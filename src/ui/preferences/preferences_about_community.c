#include "preferences_about_community.h"
#include "preferences_state.h"
#include "ui_animation.h"
#include "ui_backend.h"
#include "ui_catime.h"
#include "ui_icons.h"
#include "ui_paint.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static const char *tr(BongoCatPreferences *value, const char *key,
    const char *fallback) {
    return bongo_cat_i18n_get(value->app->i18n, key, fallback);
}

static void text(struct nk_command_buffer *canvas, struct nk_rect bounds,
    const char *value, const struct nk_user_font *font, struct nk_color color) {
    nk_draw_text(canvas, bounds, value, nk_strlen(value), font,
        nk_rgba(0, 0, 0, 0), color);
}

static void centered_span(struct nk_command_buffer *canvas,
    struct nk_rect bounds, const char *value, int length,
    const struct nk_user_font *font, struct nk_color color) {
    float width = font->width(font->userdata, font->height,
        value, length);
    nk_draw_text(canvas, nk_rect(bounds.x + (bounds.w - width) * .5f,
        bounds.y + (bounds.h - font->height) * .5f,
        NK_MIN(width + 1, bounds.w), font->height), value, length, font,
        nk_rgba(0, 0, 0, 0), color);
}

static void centered(struct nk_command_buffer *canvas, struct nk_rect bounds,
    const char *value, const struct nk_user_font *font, struct nk_color color) {
    centered_span(canvas, bounds, value, nk_strlen(value), font, color);
}

static float span_width(const struct nk_user_font *font,
    const char *value, int length) {
    return font->width(font->userdata, font->height, value, length);
}

static void span(struct nk_command_buffer *canvas, struct nk_rect bounds,
    const char *value, int length, const struct nk_user_font *font,
    struct nk_color color) {
    if (length > 0) nk_draw_text(canvas, bounds, value, length, font,
        nk_rgba(0, 0, 0, 0), color);
}

static int next_utf8_boundary(const char *value, int length, int offset) {
    int next = offset + 1;
    while (next < length &&
        (((unsigned char)value[next] & 0xc0u) == 0x80u)) ++next;
    return next;
}

static int balanced_split(const char *value, int length,
    const struct nk_user_font *font, float maximum_width,
    bool spaces_only, float *widest_line) {
    int best = -1;
    float best_width = 1.0e30f;
    for (int split = next_utf8_boundary(value, length, 0);
        split < length; split = next_utf8_boundary(value, length, split)) {
        if (spaces_only && value[split] != ' ') continue;
        int left = split, right = split;
        while (left > 0 && value[left - 1] == ' ') --left;
        while (right < length && value[right] == ' ') ++right;
        if (!left || right >= length) continue;
        float left_width = font->width(font->userdata, font->height,
            value, left);
        float right_width = font->width(font->userdata, font->height,
            value + right, length - right);
        float width = NK_MAX(left_width, right_width);
        if (width < best_width) { best = split; best_width = width; }
    }
    if (widest_line) *widest_line = best_width;
    return best_width <= maximum_width ? best : -1;
}

static void centered_wrapped(struct nk_command_buffer *canvas,
    struct nk_rect bounds, const char *value,
    const struct nk_user_font *font, struct nk_color color) {
    int length = nk_strlen(value);
    if (font->width(font->userdata, font->height, value, length) <= bounds.w) {
        centered_span(canvas, bounds, value, length, font, color);
        return;
    }
    float widest = 0.0f;
    int split = balanced_split(value, length, font, bounds.w, true, &widest);
    if (split < 0)
        split = balanced_split(value, length, font, bounds.w, false, &widest);
    if (split < 0) {
        centered_span(canvas, bounds, value, length, font, color);
        return;
    }
    int left = split, right = split;
    while (left > 0 && value[left - 1] == ' ') --left;
    while (right < length && value[right] == ' ') ++right;
    struct nk_rect line = nk_rect(bounds.x,
        bounds.y + (bounds.h - font->height * 2.0f) * .5f,
        bounds.w, font->height);
    centered_span(canvas, line, value, left, font, color);
    line.y += font->height;
    centered_span(canvas, line, value + right, length - right, font, color);
}

static bool hit(struct nk_context *context, struct nk_rect bounds) {
    return nk_input_is_mouse_hovering_rect(&context->input, bounds) &&
        nk_input_is_mouse_click_in_rect(&context->input, NK_BUTTON_LEFT, bounds);
}

static void open_url(const char *url) {
    if (!SDL_OpenURL(url)) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "Cannot open URL: %s", SDL_GetError());
}

static void community_link(BongoCatPreferences *value,
    struct nk_context *context, struct nk_command_buffer *canvas,
    struct nk_rect bounds, int index, BongoCatUIPalette p) {
    const char *labels[] = {"Discord", tr(value,
        "native.support.qqGroup", "QQ Group")};
    const char *details[] = {"https://discord.gg/vf8jqnattk", "473033518"};
    const char *urls[] = {"https://discord.gg/vf8jqnattk",
        "https://qm.qq.com/q/sccLv3USXg"};
    struct nk_color brand = index ? p.accent : nk_rgb(88, 101, 242);
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, bounds);
    char id[32]; snprintf(id, sizeof(id), "community-hover-%d", index);
    float lift = bongo_cat_ui_animate_eased(context, id,
        hover ? 1.0f : 0.0f, 220.0f, BONGO_CAT_UI_EASE_SWIFT);
    bounds.y -= 2.0f * lift;
    struct nk_color background = bongo_cat_ui_color_mix(p.surface_glass,
        brand, hover ? .10f : .055f);
    if (hover && p.effects) bongo_cat_ui_paint_shadow(context, bounds, 18,
        0, 10, 24, 0, nk_rgba(brand.r, brand.g, brand.b, 28));
    nk_fill_rect(canvas, bounds, 18, background);
    struct nk_rect mark = nk_rect(bounds.x + 12, bounds.y + 12, 44, 44);
    nk_fill_rect(canvas, mark, 14, brand);
    bongo_cat_preferences_icon_draw(value, canvas,
        index ? BONGO_CAT_UI_ICON_QQ : BONGO_CAT_UI_ICON_DISCORD,
        nk_rect(mark.x + 9.5f, mark.y + 9.5f, 25, 25), nk_rgb(255, 255, 255));
    text(canvas, nk_rect(bounds.x + 68, bounds.y + 13, bounds.w - 94, 22),
        labels[index], value->ui.label_font, p.text);
    text(canvas, nk_rect(bounds.x + 68, bounds.y + 37, bounds.w - 94, 18),
        details[index], value->ui.caption_font, p.muted);
    nk_stroke_line(canvas, bounds.x + bounds.w - 22, bounds.y + 31,
        bounds.x + bounds.w - 17, bounds.y + 34, 1.5f, brand);
    nk_stroke_line(canvas, bounds.x + bounds.w - 17, bounds.y + 34,
        bounds.x + bounds.w - 22, bounds.y + 37, 1.5f, brand);
    if (hover) bongo_cat_ui_cursor_hover_rect(context, bounds,
        BONGO_CAT_UI_CURSOR_POINTER);
    if (hit(context, bounds)) open_url(urls[index]);
}

static void coffee(BongoCatPreferences *value, struct nk_context *context,
    struct nk_command_buffer *canvas, struct nk_rect bounds,
    BongoCatUIPalette p) {
    const char *label = tr(value, "native.support.coffee",
        "Buy the development team a coffee?");
    float width = value->ui.caption_font->width(value->ui.caption_font->userdata,
        value->ui.caption_font->height, label, nk_strlen(label)) + 48.0f;
    struct nk_rect pill = nk_rect(bounds.x + (bounds.w - width) * .5f,
        bounds.y, width, 36);
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, pill);
    if (p.effects) bongo_cat_ui_paint_shadow(context, pill, 18, 0,
        hover ? 9.0f : 7.0f, hover ? 22.0f : 18.0f, 0,
        nk_rgba(p.pink.r, p.pink.g, p.pink.b, hover ? 77 : 61));
    nk_fill_rect(canvas, pill, 18, p.pink);
    bongo_cat_preferences_icon_draw(value, canvas,
        BONGO_CAT_UI_ICON_COFFEE,
        nk_rect(pill.x + 16, pill.y + 10, 16, 16), nk_rgb(255, 255, 255));
    centered(canvas, nk_rect(pill.x + 32, pill.y, pill.w - 38, pill.h), label,
        value->ui.caption_font, nk_rgb(255, 255, 255));
    if (hover) bongo_cat_ui_cursor_hover_rect(context, pill,
        BONGO_CAT_UI_CURSOR_POINTER);
    if (hit(context, pill)) open_url("https://bongocat.pet/support");
}

void bongo_cat_preferences_about_projects_heading(
    BongoCatPreferences *value, struct nk_context *context,
    struct nk_rect bounds) {
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    centered(canvas, nk_rect(bounds.x, bounds.y + 20, bounds.w, 30),
        tr(value, "native.support.works", "More apps"),
        value->ui.heading_font, p.text);
    const char *caption = tr(value, "native.support.worksText",
        "More software from vladelaina");
    static const char developer_name[] = "vladelaina";
    const char *developer = strstr(caption, developer_name);
    int prefix = developer ? (int)(developer - caption) : nk_strlen(caption);
    int name_length = developer ? (int)(sizeof(developer_name) - 1) : 0;
    const char *suffix = developer ? developer + name_length : caption + prefix;
    int suffix_length = nk_strlen(suffix);
    float prefix_width = span_width(value->ui.caption_font, caption, prefix);
    float name_width = span_width(value->ui.caption_font,
        developer ? developer : "", name_length);
    float suffix_width = span_width(value->ui.caption_font, suffix, suffix_length);
    float x = bounds.x + (bounds.w - prefix_width - name_width - suffix_width) * .5f;
    float y = bounds.y + 49;
    span(canvas, nk_rect(x, y, prefix_width + 1, 24), caption, prefix,
        value->ui.caption_font, p.muted);
    struct nk_rect link = nk_rect(x + prefix_width, y, name_width + 1, 24);
    bool hover = developer && nk_input_is_mouse_hovering_rect(
        &context->input, link);
    float amount = bongo_cat_ui_animate_eased(context, "works-author-hover",
        hover ? 1.0f : 0.0f, 200, BONGO_CAT_UI_EASE_STANDARD);
    span(canvas, link, developer ? developer : "", name_length,
        value->ui.caption_font, bongo_cat_ui_color_mix(p.accent, p.pink, amount));
    span(canvas, nk_rect(link.x + name_width, y, suffix_width + 1, 24),
        suffix, suffix_length, value->ui.caption_font, p.muted);
    if (hover) bongo_cat_ui_cursor_hover_rect(context, link,
        BONGO_CAT_UI_CURSOR_POINTER);
    if (developer && hit(context, link))
        open_url("https://vlaina.com/r/bongocat");
    centered_wrapped(canvas, nk_rect(bounds.x, bounds.y + 66, bounds.w, 42),
        tr(value, "native.support.refactorDeveloper",
        "Developer who rewrote BongoCat in C and is also its current maintainer"),
        value->ui.caption_font, p.muted);
}

void bongo_cat_preferences_about_community(
    BongoCatPreferences *value, struct nk_context *context) {
    struct nk_rect bounds;
    nk_layout_row_dynamic(context, 220, 1);
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    BongoCatUIPalette p = bongo_cat_ui_palette(
        bongo_cat_ui_dark(context));
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    centered(canvas, nk_rect(bounds.x, bounds.y, bounds.w, 30),
        tr(value, "native.support.community", "Community"),
        value->ui.heading_font, p.text);
    for (int i = 0; i < 2; ++i)
        community_link(value, context, canvas,
            nk_rect(bounds.x + bounds.w * .5f - 289 + i * 298,
            bounds.y + 48, 280, 68), i, p);
    coffee(value, context, canvas,
        nk_rect(bounds.x, bounds.y + 136, bounds.w, 36), p);
}
