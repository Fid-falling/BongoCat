#include "preferences_state.h"
#include "preferences_notice.h"
#include "ui_backend.h"
#include "ui_catime.h"
#include "ui_animation.h"

#include <SDL3/SDL.h>
#include <stdio.h>

static const char *tr(BongoCatNeoPreferences *value, const char *key,
    const char *fallback) {
    return bongo_cat_neo_i18n_get(value->app->i18n, key, fallback);
}

static float width(const struct nk_user_font *font, const char *value) {
    return font->width(font->userdata, font->height, value, nk_strlen(value));
}

static void text(struct nk_command_buffer *canvas, struct nk_rect bounds,
    const char *value, const struct nk_user_font *font, struct nk_color color) {
    nk_draw_text(canvas, bounds, value, nk_strlen(value), font,
        nk_rgba(0, 0, 0, 0), color);
}

static void centered_span(struct nk_command_buffer *canvas,
    struct nk_rect bounds, const char *value, int length,
    const struct nk_user_font *font, struct nk_color color) {
    float value_width = font->width(font->userdata, font->height, value, length);
    float target_width = NK_MIN(bounds.w, value_width + 1.0f);
    nk_draw_text(canvas, nk_rect(bounds.x + (bounds.w - target_width) * .5f,
        bounds.y + (bounds.h - font->height) * .5f, target_width,
        font->height), value, length, font, nk_rgba(0, 0, 0, 0), color);
}

static void centered(struct nk_command_buffer *canvas, struct nk_rect bounds,
    const char *value, const struct nk_user_font *font, struct nk_color color) {
    centered_span(canvas, bounds, value, nk_strlen(value), font, color);
}

static void centered_wrapped(struct nk_command_buffer *canvas,
    struct nk_rect bounds, const char *value,
    const struct nk_user_font *font, struct nk_color color) {
    int length = nk_strlen(value), split = -1; float best = 1.0e30f;
    if (width(font, value) <= bounds.w) {
        centered_span(canvas, bounds, value, length, font, color); return;
    }
    for (int i = 1; i + 1 < length; ++i) {
        if (value[i] != ' ') continue;
        float left = font->width(font->userdata, font->height, value, i);
        float right = font->width(font->userdata, font->height,
            value + i + 1, length - i - 1);
        float widest = NK_MAX(left, right);
        if (widest < best) { best = widest; split = i; }
    }
    if (split < 0) { centered(canvas, bounds, value, font, color); return; }
    struct nk_rect line = nk_rect(bounds.x,
        bounds.y + (bounds.h - font->height * 2) * .5f, bounds.w, font->height);
    centered_span(canvas, line, value, split, font, color); line.y += font->height;
    centered_span(canvas, line, value + split + 1, length - split - 1, font, color);
}

static bool hit(struct nk_context *context, struct nk_rect bounds) {
    return nk_input_is_mouse_hovering_rect(&context->input, bounds) &&
        nk_input_is_mouse_click_in_rect(&context->input, NK_BUTTON_LEFT, bounds);
}

static void link_cursor(struct nk_context *context, struct nk_rect bounds) {
    if (nk_input_is_mouse_hovering_rect(&context->input, bounds))
        bongo_cat_neo_ui_cursor_hover_rect(context, bounds,
            BONGO_CAT_NEO_UI_CURSOR_POINTER);
}

static void open_url(const char *url) {
    if (!SDL_OpenURL(url)) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "Cannot open URL: %s", SDL_GetError());
}

static void image_contain(struct nk_command_buffer *canvas, unsigned int texture,
    int image_width, int image_height, struct nk_rect bounds) {
    if (!texture || image_width < 1 || image_height < 1) return;
    float scale = NK_MIN(bounds.w / image_width, bounds.h / image_height);
    struct nk_rect target = nk_rect(bounds.x + (bounds.w - image_width * scale) * .5f,
        bounds.y + (bounds.h - image_height * scale) * .5f,
        image_width * scale, image_height * scale);
    struct nk_image image = nk_image_id((int)texture);
    nk_draw_image(canvas, target, &image, nk_rgb(255, 255, 255));
}

static void logo(BongoCatNeoPreferences *value, struct nk_context *context,
    struct nk_command_buffer *canvas, struct nk_rect bounds,
    BongoCatNeoUIPalette p) {
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, bounds);
    float lift = bongo_cat_neo_ui_animate(context, "support-logo-hover",
        hover ? 1.0f : 0.0f, 250.0f);
    struct nk_rect raised = bounds; raised.y -= 5.0f * lift;
    nk_fill_rect(canvas, raised, 36, p.accent);
    nk_stroke_rect(canvas, raised, 36, 4, p.pink);
    struct nk_rect inner = nk_rect(raised.x + 5, raised.y + 5,
        raised.w - 10, raised.h - 10);
    nk_fill_rect(canvas, inner, 31, p.surface);
    image_contain(canvas, value->logo_texture, value->logo_width,
        value->logo_height, nk_rect(inner.x + 12, inner.y + 12,
        inner.w - 24, inner.h - 24));
    link_cursor(context, bounds);
    if (hit(context, bounds)) open_url("https://bongocatneo.com");
}

static void star_button(BongoCatNeoPreferences *value,
    struct nk_context *context, struct nk_command_buffer *canvas,
    struct nk_rect bounds, BongoCatNeoUIPalette p) {
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, bounds);
    if (hover) bounds.y -= 2;
    nk_fill_rect(canvas, bounds, 23, hover ? p.pink_hover : p.pink);
    nk_stroke_circle(canvas, nk_rect(bounds.x + 22, bounds.y + 14, 17, 17),
        1.5f, nk_rgb(255, 255, 255));
    centered(canvas, nk_rect(bounds.x + 42, bounds.y, bounds.w - 49, bounds.h),
        "Star on GitHub", value->ui.caption_font, nk_rgb(255, 255, 255));
    link_cursor(context, bounds);
    if (hit(context, bounds))
        open_url("https://github.com/vladelaina/BongoCatNeo");
}

static void hero_title(BongoCatNeoPreferences *value,
    struct nk_command_buffer *canvas, struct nk_rect bounds,
    BongoCatNeoUIPalette p) {
    const char *title = "Bongo Cat Neo";
    const char *by = "by vladelaina";
    float gap = 8, title_width = width(value->ui.heading_font, title);
    float by_width = width(value->ui.caption_font, by);
    float x = bounds.x + (bounds.w - title_width - by_width - gap) * .5f;
    text(canvas, nk_rect(x, bounds.y, title_width + 1, 32), title,
        value->ui.heading_font, p.text);
    text(canvas, nk_rect(x + title_width + gap, bounds.y + 8,
        by_width + 1, 22), by, value->ui.caption_font, p.accent);
}

static void footer(BongoCatNeoPreferences *value, struct nk_context *context,
    struct nk_command_buffer *canvas, struct nk_rect bounds,
    BongoCatNeoUIPalette p) {
    const char *version_label = tr(value, "native.support.version", "App version");
    const char *update = tr(value, "native.support.checkUpdate", "Check for updates");
    const char *feedback = tr(value, "native.support.feedback", "Feedback");
    char version[32]; snprintf(version, sizeof(version), "v%s", BONGO_CAT_NEO_VERSION);
    float label_width = width(value->ui.caption_font, version_label) + 2;
    float version_width = width(value->ui.label_font, version) + 2;
    float update_width = NK_MAX(110.0f,
        width(value->ui.caption_font, update) + 28.0f);
    float feedback_width = NK_MAX(70.0f,
        width(value->ui.caption_font, feedback) + 12.0f);
    float total = label_width + version_width + update_width +
        feedback_width + 36.0f;
    bool stacked = total > bounds.w;
    float info_width = label_width + version_width + 8.0f;
    float x = bounds.x + (bounds.w - (stacked ? info_width : total)) * .5f;
    float info_y = bounds.y + (stacked ? -8.0f : 0.0f);
    centered(canvas, nk_rect(x, info_y, label_width, 36), version_label,
        value->ui.caption_font, p.muted);
    centered(canvas, nk_rect(x + label_width + 8, info_y,
        version_width, 36), version, value->ui.label_font, p.text);
    float actions_width = update_width + feedback_width + 14.0f;
    float actions_x = stacked ? bounds.x + (bounds.w - actions_width) * .5f :
        x + info_width + 14.0f;
    float actions_y = bounds.y + (stacked ? 22.0f : 0.0f);
    struct nk_rect update_button = nk_rect(actions_x, actions_y, update_width, 36);
    nk_fill_rect(canvas, update_button, 10, p.accent);
    centered(canvas, update_button, update, value->ui.caption_font,
        nk_rgb(255, 255, 255));
    link_cursor(context, update_button);
    if (hit(context, update_button))
        bongo_cat_neo_preferences_notice_show(value->app, tr(value,
            "native.support.latest", "Already up to date"), false);
    struct nk_rect feedback_link = nk_rect(actions_x + update_width + 14,
        actions_y, feedback_width, 36);
    centered(canvas, feedback_link, feedback, value->ui.caption_font, p.accent);
    link_cursor(context, feedback_link);
    if (hit(context, feedback_link))
        open_url("https://github.com/vladelaina/BongoCatNeo/issues");
}

static void hero(BongoCatNeoPreferences *value, struct nk_context *context) {
    struct nk_rect bounds;
    nk_layout_row_dynamic(context, 320, 1);
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    BongoCatNeoUIPalette p = bongo_cat_neo_ui_palette(bongo_cat_neo_ui_dark(context));
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    for (int i = 0; i < 14; ++i) {
        float inset = (float)i * 14.0f;
        nk_fill_circle(canvas, nk_rect(bounds.x + bounds.w * .5f - 360 + inset,
            bounds.y - 205 + inset * .6f, 720 - inset * 2,
            390 - inset * 1.2f), nk_rgba(84, 174, 255, 1));
    }
    for (int i = 0; i < 10; ++i) {
        float inset = (float)i * 14.0f;
        nk_fill_circle(canvas, nk_rect(bounds.x + bounds.w * .5f - 300 + inset,
            bounds.y - 165 + inset * .6f, 600 - inset * 2,
            310 - inset * 1.2f), nk_rgba(247, 125, 170, 1));
    }
    star_button(value, context, canvas,
        nk_rect(bounds.x + bounds.w - 196, bounds.y + 6, 177, 46), p);
    logo(value, context, canvas, nk_rect(bounds.x + (bounds.w - 144) * .5f,
        bounds.y + 19, 144, 144), p);
    hero_title(value, canvas, nk_rect(bounds.x, bounds.y + 181, bounds.w, 36), p);
    centered_wrapped(canvas, nk_rect(bounds.x + 36, bounds.y + 215,
        bounds.w - 72, 40),
        tr(value, "native.support.heroText",
        "Thank you for your support. Every use and share helps Bongo Cat Neo grow."),
        value->ui.caption_font, p.muted);
    footer(value, context, canvas,
        nk_rect(bounds.x, bounds.y + 260, bounds.w, 40), p);
}

static void project(BongoCatNeoPreferences *value, struct nk_context *context,
    struct nk_command_buffer *canvas, struct nk_rect bounds, unsigned int texture,
    int image_width, int image_height, const char *name, const char *url,
    bool pink, BongoCatNeoUIPalette p) {
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, bounds);
    char animation_id[48];
    snprintf(animation_id, sizeof(animation_id), "project-hover-%s", name);
    float lift = bongo_cat_neo_ui_animate(context, animation_id,
        hover ? 1.0f : 0.0f, 280.0f);
    struct nk_rect icon = nk_rect(bounds.x + (bounds.w - 148) * .5f,
        bounds.y - 7.0f * lift, 148, 148);
    nk_fill_rect(canvas, icon, 37, pink ? p.hover_pink : p.selection);
    image_contain(canvas, texture, image_width, image_height,
        nk_rect(icon.x + 5, icon.y + 5, icon.w - 10, icon.h - 10));
    centered(canvas, nk_rect(bounds.x, bounds.y + 166, bounds.w, 30), name,
        value->ui.label_font, hover ? (pink ? p.pink : p.accent) : p.text);
    link_cursor(context, bounds);
    if (hit(context, bounds)) open_url(url);
}

static void projects(BongoCatNeoPreferences *value, struct nk_context *context) {
    struct nk_rect bounds;
    nk_layout_row_dynamic(context, 330, 1);
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    BongoCatNeoUIPalette p = bongo_cat_neo_ui_palette(bongo_cat_neo_ui_dark(context));
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    nk_stroke_line(canvas, bounds.x + 8, bounds.y - 5, bounds.x + bounds.w - 8,
        bounds.y - 5, 1, p.border);
    centered(canvas, nk_rect(bounds.x, bounds.y + 20, bounds.w, 30),
        tr(value, "native.support.works", "More apps"),
        value->ui.heading_font, p.text);
    centered(canvas, nk_rect(bounds.x, bounds.y + 49, bounds.w, 24),
        tr(value, "native.support.worksText", "More software from vladelaina"),
        value->ui.caption_font, p.muted);
    float card_width = 220, center = bounds.x + bounds.w * .5f;
    project(value, context, canvas, nk_rect(center - 262, bounds.y + 100,
        card_width, 200), value->catime_texture, value->catime_width,
        value->catime_height, "Catime", "https://cati.me/", false, p);
    project(value, context, canvas, nk_rect(center + 42, bounds.y + 100,
        card_width, 200), value->vlaina_texture, value->vlaina_width,
        value->vlaina_height, "vlaina", "https://vlaina.com/", true, p);
}

static void community(BongoCatNeoPreferences *value,
    struct nk_context *context) {
    struct nk_rect bounds;
    nk_layout_row_dynamic(context, 180, 1);
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    BongoCatNeoUIPalette p = bongo_cat_neo_ui_palette(bongo_cat_neo_ui_dark(context));
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    centered(canvas, nk_rect(bounds.x, bounds.y, bounds.w, 30),
        tr(value, "native.support.community", "Community"),
        value->ui.heading_font, p.text);
    const char *labels[] = {"Discord", "QQ Group"};
    const char *urls[] = {"https://discord.gg/vf8jqnattk",
        "https://qm.qq.com/q/jmr39ESZIk"};
    for (int i = 0; i < 2; ++i) {
        struct nk_rect link = nk_rect(bounds.x + bounds.w * .5f - 289 + i * 298,
            bounds.y + 48, 280, 68);
        bool hover = nk_input_is_mouse_hovering_rect(&context->input, link);
        nk_fill_rect(canvas, link, 18, hover ? p.selection : p.field);
        nk_fill_rect(canvas, nk_rect(link.x + 12, link.y + 12, 44, 44), 14,
            i ? p.accent : nk_rgb(88, 101, 242));
        text(canvas, nk_rect(link.x + 68, link.y + 15, link.w - 80, 24),
            labels[i], value->ui.label_font, p.text);
        link_cursor(context, link); if (hit(context, link)) open_url(urls[i]);
    }
}

void bongo_cat_neo_preferences_page_about(BongoCatNeoPreferences *value,
    struct nk_context *context) {
    bongo_cat_neo_preferences_support_assets_load(value);
    hero(value, context);
    projects(value, context);
    community(value, context);
}
