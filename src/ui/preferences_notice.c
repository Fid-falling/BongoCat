#include "preferences_notice.h"
#include "preferences_state.h"
#include "ui_backend.h"
#include "ui_catime.h"

#include <SDL3/SDL.h>
#include <stdio.h>

void bongo_cat_neo_preferences_notice_show(BongoCatNeoApp *app,
    const char *message, bool error) {
    if (!app || !app->preferences || !message || !message[0]) return;
    BongoCatNeoPreferences *value = app->preferences;
    snprintf(value->notice, sizeof(value->notice), "%s", message);
    value->notice_error = error;
    value->notice_started_ns = SDL_GetTicksNS();
    value->notice_until_ns = value->notice_started_ns + 4000000000ULL;
    bongo_cat_neo_preferences_invalidate(value);
}

void bongo_cat_neo_preferences_notice_draw(BongoCatNeoPreferences *value,
    struct nk_context *context, float width, float height) {
    (void)height;
    if (!value || !value->notice[0]) return;
    uint64_t now = SDL_GetTicksNS();
    if (now >= value->notice_until_ns) {
        value->notice[0] = '\0';
        value->notice_until_ns = 0;
        return;
    }
    value->render_dirty = true;
    bool dark = bongo_cat_neo_ui_dark(context);
    BongoCatNeoUIPalette p = bongo_cat_neo_ui_palette(dark);
    const struct nk_user_font *font = bongo_cat_neo_ui_body_font(context);
    float text_width = font->width(font->userdata, font->height,
        value->notice, nk_strlen(value->notice));
    float toast_width = NK_MIN(text_width + 36.0f, width - 64.0f);
    float enter = NK_MIN(1.0f,
        (now - value->notice_started_ns) / 250000000.0f);
    float leave = NK_MIN(1.0f,
        (value->notice_until_ns - now) / 250000000.0f);
    float progress = NK_MIN(enter, leave);
    float shown_width = toast_width * (.9f + .1f * progress);
    struct nk_rect bounds = nk_rect((width - shown_width) * .5f,
        20.0f - 12.0f * (1.0f - progress), shown_width, 40.0f);
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    struct nk_color tone = value->notice_error ? p.danger : p.pink;
    nk_fill_rect(canvas, nk_rect(bounds.x + 1, bounds.y + 4,
        bounds.w, bounds.h), 20, nk_rgba(tone.r, tone.g, tone.b,
        (nk_byte)(70 * progress)));
    nk_fill_rect(canvas, bounds, 20, nk_rgba(tone.r, tone.g, tone.b,
        (nk_byte)(255 * progress)));
    struct nk_rect text = nk_rect(bounds.x + (bounds.w - text_width) * .5f,
        bounds.y + (bounds.h - font->height) * .5f,
        NK_MIN(text_width + 1, bounds.w - 20), font->height);
    nk_draw_text(canvas, text, value->notice, nk_strlen(value->notice), font,
        nk_rgba(0, 0, 0, 0), nk_rgba(255, 255, 255,
        (nk_byte)(255 * progress)));
}
