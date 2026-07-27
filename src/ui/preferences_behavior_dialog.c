#include "preferences_state.h"
#include "preferences_notice.h"
#include "preferences_overlay.h"
#include "preferences_shortcut_clear.h"
#include "ui_animation.h"
#include "ui_backend.h"
#include "ui_icons.h"
#include "ui_paint.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *tr(BongoCatNeoPreferences *value, const char *key,
    const char *fallback) {
    return bongo_cat_neo_i18n_get(value->app->i18n, key, fallback);
}

static struct nk_color alpha(struct nk_color color, float amount) {
    return bongo_cat_neo_preferences_overlay_alpha(color, amount);
}

static void text(struct nk_command_buffer *canvas, struct nk_rect bounds,
    const char *value, const struct nk_user_font *font, struct nk_color color) {
    nk_draw_text(canvas, bounds, value, nk_strlen(value), font,
        nk_rgba(0, 0, 0, 0), color);
}

static void centered(struct nk_command_buffer *canvas, struct nk_rect bounds,
    const char *value, const struct nk_user_font *font, struct nk_color color) {
    float width = font->width(font->userdata, font->height, value,
        nk_strlen(value));
    text(canvas, nk_rect(bounds.x + (bounds.w - width) * .5f,
        bounds.y + (bounds.h - font->height) * .5f, width + 1, font->height),
        value, font, color);
}

static bool hit(struct nk_context *context, struct nk_rect bounds, bool enabled) {
    return enabled && nk_input_is_mouse_hovering_rect(&context->input, bounds) &&
        nk_input_is_mouse_click_in_rect(&context->input, NK_BUTTON_LEFT, bounds);
}

static BongoCatNeoBehaviorShortcut *shortcut_for(BongoCatNeoConfig *config,
    const char *id) {
    for (size_t i = 0; i < config->behavior_shortcut_count; ++i)
        if (!strcmp(config->behavior_shortcuts[i].id, id))
            return &config->behavior_shortcuts[i];
    if (config->behavior_shortcut_count >= BONGO_CAT_NEO_BEHAVIOR_CAP)
        return NULL;
    BongoCatNeoBehaviorShortcut *value =
        &config->behavior_shortcuts[config->behavior_shortcut_count++];
    memset(value, 0, sizeof(*value));
    snprintf(value->id, sizeof(value->id), "%s", id);
    return value;
}

static bool matches(const BongoCatNeoBehaviorEntry *entry, int tab) {
    return tab == 0 ? entry->kind == BONGO_CAT_NEO_BEHAVIOR_MOTION :
        entry->kind == BONGO_CAT_NEO_BEHAVIOR_EXPRESSION;
}

static size_t row_count(const BongoCatNeoPreferences *value) {
    size_t count = 0;
    for (size_t i = 0; i < value->app->behaviors.count; ++i)
        if (matches(&value->app->behaviors.entries[i], value->behavior_tab))
            count++;
    return count;
}

bool bongo_cat_neo_preferences_behavior_dialog_active(
    const BongoCatNeoPreferences *value) {
    return value && value->behavior_dialog;
}

void bongo_cat_neo_preferences_behavior_dialog_open(
    BongoCatNeoPreferences *value) {
    if (!value) return;
    value->behavior_dialog = true;
    value->behavior_dialog_opened_ns = SDL_GetTicksNS();
    value->behavior_dialog_closing_ns = 0;
    value->render_dirty = true;
}

void bongo_cat_neo_preferences_behavior_dialog_close(
    BongoCatNeoPreferences *value) {
    if (!value || !value->behavior_dialog || value->behavior_dialog_closing_ns)
        return;
    value->behavior_dialog_closing_ns = SDL_GetTicksNS();
    bongo_cat_neo_preferences_shortcut_cancel(value);
    value->render_dirty = true;
}

static bool draw_header(BongoCatNeoPreferences *value, struct nk_context *context,
    struct nk_command_buffer *canvas, struct nk_rect panel,
    BongoCatNeoUIPalette p, float opacity, bool enabled) {
    text(canvas, nk_rect(panel.x + 20, panel.y + 21, panel.w - 74, 24),
        tr(value, "pages.preference.model.behaviorModal.title",
        "Motions and expressions"), value->ui.label_font, alpha(p.text, opacity));
    struct nk_rect close = nk_rect(panel.x + panel.w - 52, panel.y + 17, 32, 32);
    bool hover = enabled && nk_input_is_mouse_hovering_rect(&context->input, close);
    nk_fill_rect(canvas, close, 8, alpha(hover ? p.hover_pink : p.field, opacity));
    bongo_cat_neo_preferences_icon_draw(value, canvas, BONGO_CAT_NEO_UI_ICON_CLOSE,
        nk_rect(close.x + 7, close.y + 7, 18, 18),
        alpha(hover ? p.pink : p.muted, opacity));
    if (hover) bongo_cat_neo_ui_cursor_hover_rect(context, close,
        BONGO_CAT_NEO_UI_CURSOR_POINTER);
    return hit(context, close, enabled);
}

static void draw_segments(BongoCatNeoPreferences *value,
    struct nk_context *context, struct nk_command_buffer *canvas,
    struct nk_rect panel, BongoCatNeoUIPalette p, float opacity, bool enabled) {
    struct nk_rect wrapper = nk_rect(panel.x + 20, panel.y + 85,
        panel.w - 40, 43);
    nk_fill_rect(canvas, wrapper, 10, alpha(p.field, opacity));
    const char *labels[] = {tr(value,
        "pages.preference.model.behaviorModal.labels.motion", "Motions"), tr(value,
        "pages.preference.model.behaviorModal.labels.expression", "Expressions")};
    float width = (wrapper.w - 6) * .5f;
    for (int i = 0; i < 2; ++i) {
        struct nk_rect button = nk_rect(wrapper.x + 3 + width * i,
            wrapper.y + 3, width, 37);
        bool hover = enabled && nk_input_is_mouse_hovering_rect(
            &context->input, button);
        char id[32]; snprintf(id, sizeof(id), "behavior-segment-%d", i);
        float active = bongo_cat_neo_ui_animate_eased(context, id,
            value->behavior_tab == i ? 1.0f : 0.0f, 200,
            BONGO_CAT_NEO_UI_EASE_STANDARD);
        struct nk_color background = bongo_cat_neo_ui_color_mix(
            hover ? p.hover : p.field, p.accent, active);
        if (active > .01f && p.effects) bongo_cat_neo_ui_paint_shadow(context,
            button, 8, 0, 2, 10, 0, alpha(nk_rgba(p.accent.r,
            p.accent.g, p.accent.b, 89), opacity * active));
        nk_fill_rect(canvas, button, 8, alpha(background, opacity));
        centered(canvas, button, labels[i], value->ui.caption_font,
            alpha(bongo_cat_neo_ui_color_mix(p.muted,
            nk_rgb(255, 255, 255), active), opacity));
        if (hover) bongo_cat_neo_ui_cursor_hover_rect(context, button,
            BONGO_CAT_NEO_UI_CURSOR_POINTER);
        if (hit(context, button, enabled) && value->behavior_tab != i) {
            value->behavior_tab = i;
            value->behavior_tab_transition_ns = SDL_GetTicksNS();
            bongo_cat_neo_preferences_shortcut_cancel(value);
        }
    }
}

static bool editor(BongoCatNeoPreferences *value, struct nk_context *context,
    struct nk_command_buffer *canvas, struct nk_rect bounds, const char *id,
    BongoCatNeoBehaviorShortcut *shortcut, BongoCatNeoUIPalette p,
    float opacity, bool enabled) {
    bool active = bongo_cat_neo_preferences_shortcut_active(value, id);
    bool hover = enabled && nk_input_is_mouse_hovering_rect(&context->input, bounds);
    if (active && p.effects) {
        float phase = (float)(SDL_GetTicksNS() % 1500000000ULL) / 1500000000.0f;
        float pulse = sinf(phase * 6.2831853f);
        bongo_cat_neo_ui_paint_shadow(context, bounds, 10, 0, 0,
            5 + 3 * pulse, 0, alpha(nk_rgba(p.pink.r, p.pink.g,
            p.pink.b, 76), opacity));
        value->render_dirty = true;
    }
    nk_fill_rect(canvas, bounds, 10, alpha(active ? p.hover_pink :
        (hover ? p.hover : p.field), opacity));
    nk_stroke_rect(canvas, bounds, 10, 1, alpha(active ? p.pink :
        (hover ? p.accent : p.border_subtle), opacity));
    bongo_cat_neo_preferences_icon_draw(value, canvas,
        BONGO_CAT_NEO_UI_ICON_KEYBOARD, nk_rect(bounds.x + 13,
        bounds.y + 9, 18, 18), alpha(active ? p.pink : p.muted, opacity));
    const char *shown = active ? tr(value,
        "components.shortcut.hints.pressRecordShortcut", "Press shortcut") :
        (shortcut->shortcut[0] ? shortcut->shortcut : tr(value,
        "components.shortcut.hints.clickRecordShortcut", "Click to record shortcut"));
    text(canvas, nk_rect(bounds.x + 39, bounds.y + 8,
        bounds.w - (shortcut->shortcut[0] && !active ? 70 : 48), 21), shown,
        value->ui.caption_font,
        alpha(active ? p.pink : (hover ? p.accent : p.muted), opacity));
    struct nk_rect clear = nk_rect(bounds.x + bounds.w - 25,
        bounds.y + 8, 17, 20);
    if (bongo_cat_neo_pref_shortcut_clear(context, canvas, id, clear, p,
        opacity, enabled && !active && shortcut->shortcut[0])) {
        shortcut->shortcut[0] = '\0'; value->render_dirty = true; return false;
    }
    if (hover) bongo_cat_neo_ui_cursor_hover_rect(context, bounds,
        BONGO_CAT_NEO_UI_CURSOR_POINTER);
    return hit(context, bounds, enabled);
}

static void draw_row(BongoCatNeoPreferences *value, struct nk_context *context,
    struct nk_command_buffer *canvas, struct nk_rect row,
    BongoCatNeoBehaviorEntry *entry, BongoCatNeoUIPalette p,
    float opacity, bool enabled) {
    text(canvas, nk_rect(row.x + 16, row.y + 18, row.w - 268, 21),
        entry->label, value->ui.caption_font, alpha(p.text, opacity));
    struct nk_rect play = nk_rect(row.x + row.w - 52, row.y + 10, 36, 36);
    struct nk_rect edit = nk_rect(play.x - 188, row.y + 10, 180, 36);
    bool play_hover = enabled && nk_input_is_mouse_hovering_rect(
        &context->input, play);
    nk_fill_rect(canvas, play, 10, alpha(play_hover ? p.hover : p.field, opacity));
    nk_stroke_rect(canvas, play, 10, 1, alpha(play_hover ? p.accent :
        p.border_subtle, opacity));
    bongo_cat_neo_preferences_icon_draw(value, canvas, BONGO_CAT_NEO_UI_ICON_PLAY,
        nk_rect(play.x + 10, play.y + 10, 16, 16),
        alpha(play_hover ? p.accent : p.text, opacity));
    if (play_hover) bongo_cat_neo_ui_cursor_hover_rect(context, play,
        BONGO_CAT_NEO_UI_CURSOR_POINTER);
    if (hit(context, play, enabled)) {
        bongo_cat_neo_app_run_behavior(value->app, entry);
        char message[sizeof(entry->label) + 5];
        snprintf(message, sizeof(message), "%s \xE2\x9C\x93", entry->label);
        bongo_cat_neo_preferences_notice_show(value->app, message, false);
    }
    BongoCatNeoBehaviorShortcut *shortcut = shortcut_for(&value->app->config,
        entry->id);
    if (!shortcut) return;
    char id[BONGO_CAT_NEO_ID_CAP + 16];
    snprintf(id, sizeof(id), "behavior-%.*s", (int)sizeof(id) - 10, entry->id);
    if (editor(value, context, canvas, edit, id, shortcut, p, opacity, enabled))
        bongo_cat_neo_preferences_shortcut_begin(value, id,
            shortcut->shortcut, sizeof(shortcut->shortcut));
}

static void draw_rows(BongoCatNeoPreferences *value, struct nk_context *context,
    struct nk_command_buffer *canvas, struct nk_rect panel,
    BongoCatNeoUIPalette p, float opacity, bool enabled, size_t count) {
    struct nk_rect viewport = nk_rect(panel.x + 20, panel.y + 144,
        panel.w - 40, panel.h - 164);
    float maximum = NK_MAX(0.0f, count * 56.0f - viewport.h);
    if (enabled && nk_input_is_mouse_hovering_rect(&context->input, viewport) &&
        context->input.mouse.scroll_delta.y != 0) {
        value->behavior_scroll[value->behavior_tab] = NK_CLAMP(0.0f,
            value->behavior_scroll[value->behavior_tab] -
            context->input.mouse.scroll_delta.y * 48.0f, maximum);
        value->render_dirty = true;
    }
    float offset = NK_CLAMP(0.0f, value->behavior_scroll[value->behavior_tab], maximum);
    float content_opacity = opacity;
    if (value->behavior_tab_transition_ns) {
        float progress = (float)(SDL_GetTicksNS() -
            value->behavior_tab_transition_ns) / 180000000.0f;
        if (progress >= 1.0f) value->behavior_tab_transition_ns = 0;
        else {
            float eased = bongo_cat_neo_ui_ease(BONGO_CAT_NEO_UI_EASE_SWIFT,
                NK_CLAMP(0.0f, progress, 1.0f));
            content_opacity *= eased; offset += 5.0f * (1.0f - eased);
            value->render_dirty = true;
        }
    }
    nk_push_scissor(canvas, viewport);
    size_t shown = 0;
    for (size_t i = 0; i < value->app->behaviors.count; ++i) {
        BongoCatNeoBehaviorEntry *entry = &value->app->behaviors.entries[i];
        if (!matches(entry, value->behavior_tab)) continue;
        struct nk_rect row = nk_rect(viewport.x,
            viewport.y + shown++ * 56.0f - offset, viewport.w, 56);
        if (row.y + row.h >= viewport.y && row.y <= viewport.y + viewport.h)
            draw_row(value, context, canvas, row, entry, p,
                content_opacity, enabled);
    }
    nk_push_scissor(canvas, nk_window_get_content_region(context));
    if (!shown) centered(canvas, viewport, tr(value, "native.noBehaviors",
        "No items"), value->ui.caption_font, alpha(p.muted, content_opacity));
}

void bongo_cat_neo_preferences_behavior_dialog_draw(
    BongoCatNeoPreferences *value, struct nk_context *context) {
    if (!bongo_cat_neo_preferences_behavior_dialog_active(value)) return;
    bongo_cat_neo_ui_cursor_reset(context);
    struct nk_rect region = nk_window_get_content_region(context);
    size_t count = row_count(value);
    float width = NK_MIN(540.0f, region.w - 48.0f);
    float height = NK_MIN(165.0f + count * 56.0f, region.h - 48.0f);
    BongoCatNeoOverlayFrame frame = bongo_cat_neo_preferences_overlay_frame(
        region, width, height, value->behavior_dialog_opened_ns,
        value->behavior_dialog_closing_ns);
    if (frame.finished) {
        value->behavior_dialog = false;
        value->behavior_dialog_opened_ns = value->behavior_dialog_closing_ns = 0;
        return;
    }
    BongoCatNeoUIPalette p = bongo_cat_neo_ui_palette(bongo_cat_neo_ui_dark(context));
    bongo_cat_neo_preferences_overlay_draw(context, region, &frame, p);
    bool closing = value->behavior_dialog_closing_ns != 0;
    bool input_ready = SDL_GetTicksNS() - value->behavior_dialog_opened_ns >=
        50000000ULL;
    float opacity = closing ? frame.visibility : 1.0f;
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    nk_fill_rect(canvas, frame.panel, 18, alpha(p.surface, opacity));
    nk_stroke_rect(canvas, frame.panel, 18, 1, alpha(p.border, opacity));
    bool close = draw_header(value, context, canvas, frame.panel, p,
        opacity, !closing && input_ready);
    draw_segments(value, context, canvas, frame.panel, p, opacity,
        !closing && input_ready);
    draw_rows(value, context, canvas, frame.panel, p, opacity,
        !closing && input_ready, count);
    bool outside = hit(context, region, !closing && input_ready) &&
        !nk_input_is_mouse_hovering_rect(&context->input, frame.panel);
    if (close || outside) bongo_cat_neo_preferences_behavior_dialog_close(value);
    if (frame.visibility < 1.0f || closing) value->render_dirty = true;
}
