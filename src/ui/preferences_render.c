#include "preferences_state.h"
#include "preferences_controls.h"
#include "preferences_notice.h"
#include "ui_catime.h"
#include "ui_animation.h"
#include "bongo_cat_neo/file.h"
#include "bongo_cat_neo/path.h"
#include "bongo_cat_neo/tray.h"

#include <SDL3/SDL_opengl.h>
#include <math.h>
#include <stdio.h>

typedef struct RootStyle {
    struct nk_vec2 padding;
    struct nk_vec2 group_padding;
    struct nk_vec2 spacing;
    struct nk_style_item fixed;
    struct nk_color background;
    float group_border;
} RootStyle;

static const char *tr(const BongoCatNeoPreferences *value, const char *key,
    const char *fallback) {
    return bongo_cat_neo_i18n_get(value->app->i18n, key, fallback);
}

static void draw_icon(void *userdata, struct nk_command_buffer *canvas,
    int icon, struct nk_rect bounds, struct nk_color color) {
    bongo_cat_neo_preferences_icon_draw(userdata, canvas, icon, bounds, color);
}

static void draw_page(BongoCatNeoPreferences *value, struct nk_context *context) {
    switch (value->page) {
    case 0: bongo_cat_neo_preferences_page_cat(value->app, context); break;
    case 1: bongo_cat_neo_preferences_page_general(value->app, context); break;
    case 2: bongo_cat_neo_preferences_page_model(value, context); break;
    case 3: bongo_cat_neo_preferences_page_shortcuts(value, context); break;
    default: bongo_cat_neo_preferences_page_about(value, context); break;
    }
}

static RootStyle root_style_save(struct nk_context *context) {
    RootStyle saved;
    saved.padding = context->style.window.padding;
    saved.group_padding = context->style.window.group_padding;
    saved.spacing = context->style.window.spacing;
    saved.fixed = context->style.window.fixed_background;
    saved.background = context->style.window.background;
    saved.group_border = context->style.window.group_border;
    return saved;
}

static void root_style_apply(struct nk_context *context,
    BongoCatNeoUIPalette palette) {
    context->style.window.padding = nk_vec2(BONGO_CAT_NEO_UI_MARGIN,
        BONGO_CAT_NEO_UI_MARGIN);
    context->style.window.group_padding = nk_vec2(0, 0);
    context->style.window.spacing = nk_vec2(0, 0);
    context->style.window.fixed_background = nk_style_item_color(
        palette.background);
    context->style.window.background = palette.background;
    context->style.window.group_border = 0;
}

static void root_style_restore(struct nk_context *context,
    const RootStyle *saved) {
    context->style.window.padding = saved->padding;
    context->style.window.group_padding = saved->group_padding;
    context->style.window.spacing = saved->spacing;
    context->style.window.fixed_background = saved->fixed;
    context->style.window.background = saved->background;
    context->style.window.group_border = saved->group_border;
}

static bool draw_shell(BongoCatNeoPreferences *value, struct nk_context *context,
    int width, int height, bool dark) {
    static const char *page_ids[] = {
        "page-cat", "page-general", "page-model", "page-shortcuts", "page-about"};
    const char *menus[] = {
        tr(value, "pages.preference.cat.title", "Cat"),
        tr(value, "pages.preference.general.title", "General"),
        tr(value, "pages.preference.model.title", "Model"),
        tr(value, "pages.preference.shortcut.title", "Shortcuts"),
        tr(value, "native.support.title", "Support Developer")};
    bool modal = bongo_cat_neo_preferences_remove_dialog_active(value->app) ||
        bongo_cat_neo_preferences_behavior_dialog_active(value);
    BongoCatNeoUIPalette p = bongo_cat_neo_ui_palette(dark);
    bongo_cat_neo_ui_shell_draw(context, (float)width, (float)height, dark);
    float sidebar = bongo_cat_neo_ui_sidebar_width((float)width);
    float interior_height = (float)height - BONGO_CAT_NEO_UI_MARGIN * 2.0f;
    nk_layout_row_begin(context, NK_STATIC, interior_height, 2);
    nk_layout_row_push(context, sidebar);
    struct nk_color clear = nk_rgba(0, 0, 0, 0);
    context->style.window.fixed_background = nk_style_item_color(clear);
    context->style.window.background = clear;
    context->style.window.group_padding = nk_vec2(0, 0);
    bool title_clicked = false;
    if (nk_group_begin(context, "preferences-sidebar", NK_WINDOW_NO_SCROLLBAR)) {
    bongo_cat_neo_ui_header(context, "Bongo Cat Neo",
        value->ui.caption_font, value->logo_texture, &title_clicked, !modal, dark);
    if (title_clicked && !SDL_OpenURL("https://bongocatneo.com"))
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Cannot open website: %s", SDL_GetError());
    bongo_cat_neo_ui_set_icons(draw_icon, value);
    bongo_cat_neo_ui_tabs(context, menus, 5, &value->page, !modal, dark,
        draw_icon, value);
    if (!value->page_seen) {
        value->page_seen = true; value->last_page = value->page;
    } else if (value->last_page != value->page) {
        value->last_page = value->page;
        value->page_transition_ns = SDL_GetTicksNS();
    }
    nk_group_end(context);
    }
    nk_layout_row_push(context, (float)width - BONGO_CAT_NEO_UI_MARGIN * 2.0f - sidebar);
    bool close_requested = false;
    if (nk_group_begin(context, "preferences-content", NK_WINDOW_NO_SCROLLBAR)) {
    close_requested = bongo_cat_neo_ui_content_header(context,
        menus[value->page], value->page, !modal, dark);
    float body_height = interior_height - BONGO_CAT_NEO_UI_HEADER_HEIGHT;
    nk_layout_row_dynamic(context, NK_MAX(120.0f, body_height), 1);
    struct nk_rect body_bounds = nk_widget_bounds(context);
    float page_progress = 1.0f;
    if (value->page_transition_ns) {
        float elapsed = (float)(SDL_GetTicksNS() - value->page_transition_ns) /
            220000000.0f;
        if (elapsed >= 1.0f) value->page_transition_ns = 0;
        else {
            page_progress = bongo_cat_neo_ui_ease(BONGO_CAT_NEO_UI_EASE_SWIFT,
                NK_CLAMP(0.0f, elapsed, 1.0f));
            value->render_dirty = true;
        }
    }
    context->style.window.group_padding = nk_vec2(24,
        16 + 6.0f * (1.0f - page_progress));
    context->style.window.spacing = nk_vec2(10, 10);
    int scroll_page = NK_CLAMP(0, value->page, 4);
    if (value->scroll_ready[scroll_page])
        nk_group_set_scroll(context, page_ids[scroll_page], 0,
            (nk_uint)NK_MAX(0.0f, value->scroll_current[scroll_page]));
    struct nk_mouse_button saved_left = context->input.mouse.buttons[NK_BUTTON_LEFT];
    struct nk_vec2 saved_wheel = context->input.mouse.scroll_delta;
    if (modal) {
        context->input.mouse.buttons[NK_BUTTON_LEFT].clicked = 0;
        context->input.mouse.buttons[NK_BUTTON_LEFT].down = nk_false;
        context->input.mouse.scroll_delta = nk_vec2(0, 0);
    }
    if (nk_group_begin(context, page_ids[value->page], 0)) {
        draw_page(value, context);
        float wheel = context->input.mouse.scroll_delta.y;
        context->input.mouse.scroll_delta.y = 0;
        nk_group_end(context);
        nk_uint group_x = 0, group_y = 0;
        nk_group_get_scroll(context, page_ids[value->page], &group_x, &group_y);
        float actual = (float)group_y;
        if (!value->scroll_ready[scroll_page]) {
            value->scroll_ready[scroll_page] = true;
            value->scroll_current[scroll_page] = actual;
            value->scroll_target[scroll_page] = actual;
        }
        if (wheel != 0.0f)
            value->scroll_target[scroll_page] = NK_MAX(0.0f,
                actual - wheel * 72.0f);
        else if (fabsf(actual - value->scroll_current[scroll_page]) > 2.0f)
            value->scroll_target[scroll_page] = actual;
        float next = actual + (value->scroll_target[scroll_page] - actual) * .24f;
        if (fabsf(next - value->scroll_target[scroll_page]) < .5f)
            next = value->scroll_target[scroll_page];
        value->scroll_current[scroll_page] = next;
        if (wheel != 0.0f || fabsf(next - value->scroll_target[scroll_page]) > .5f)
            value->render_dirty = true;
    }
    if (modal) {
        context->input.mouse.buttons[NK_BUTTON_LEFT] = saved_left;
        context->input.mouse.scroll_delta = saved_wheel;
    }
    if (page_progress < 1.0f) {
        struct nk_command_buffer *canvas = nk_window_get_canvas(context);
        nk_fill_rect(canvas, body_bounds, 0, nk_rgba(p.surface.r,
            p.surface.g, p.surface.b,
            (nk_byte)(255 * (1.0f - page_progress))));
    }
    nk_group_end(context);
    }
    nk_layout_row_end(context);
    context->style.window.fixed_background = nk_style_item_color(p.surface);
    context->style.window.background = p.surface;
    bongo_cat_neo_preferences_notice_draw(value, context, (float)width, (float)height);
    bongo_cat_neo_preferences_remove_dialog_draw(value->app, context);
    bongo_cat_neo_preferences_behavior_dialog_draw(value, context);
    return close_requested;
}

static bool draw_frame(BongoCatNeoPreferences *value, int width, int height,
    bool dark) {
    struct nk_context *context = &value->ui.context;
    RootStyle saved = root_style_save(context);
    BongoCatNeoUIPalette palette = bongo_cat_neo_ui_palette(dark);
    root_style_apply(context, palette);
    bool close_requested = false;
    if (nk_begin(context, BONGO_CAT_NEO_NAME,
        nk_rect(0, 0, (float)width, (float)height), NK_WINDOW_NO_SCROLLBAR))
        close_requested = draw_shell(value, context, width, height, dark);
    nk_end(context);
    root_style_restore(context, &saved);
    return close_requested;
}

static void write_smoke_frame(BongoCatNeoPreferences *value) {
    if (!value->app->smoke || value->frame_checked) return;
    value->frame_checked = true;
    char path[BONGO_CAT_NEO_PATH_CAP];
    bongo_cat_neo_path_join(path, sizeof(path), value->app->data_root, "ui-frame.txt");
    FILE *file = bongo_cat_neo_file_open(path, "wb");
    if (file) {
        fprintf(file, "valid=%d convert=%d vertices=%zu elements=%zu commands=%u "
            "draw_elements=%u gl_error=%u alpha_vertices=%u max_alpha=%u "
            "font_path=%d font_file=%d custom_font=%d font_probe=%d\n",
            bongo_cat_neo_ui_frame_valid(&value->ui), value->ui.last_convert_result,
            value->ui.last_vertex_bytes, value->ui.last_element_bytes,
            value->ui.last_draw_commands, value->ui.last_draw_elements,
            (unsigned)value->ui.last_gl_error, value->ui.nonzero_alpha_vertices,
            value->ui.max_alpha, value->ui.font_path_found,
            value->ui.font_file_loaded, value->ui.custom_font_loaded,
            value->ui.font_probe_loaded);
        fclose(file);
    }
    if (!bongo_cat_neo_ui_frame_valid(&value->ui)) value->app->exit_code = 1;
}

static bool reload_language(BongoCatNeoPreferences *value) {
    if (value->font_language == value->app->config.app.language) return false;
    BongoCatNeoError error = {0};
    if (!value->app->i18n || bongo_cat_neo_i18n_reload(value->app->i18n,
        value->app->config.app.language, &error) != BONGO_CAT_NEO_OK) {
        value->app->config.app.language = value->font_language;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
        return false;
    }
    value->font_language = value->app->config.app.language;
    if (value->app->tray) bongo_cat_neo_tray_sync(value->app->tray);
    value->render_dirty = true;
    return true;
}

void bongo_cat_neo_preferences_render(BongoCatNeoPreferences *value) {
    if (!value || !value->window) return;
    uint64_t now = SDL_GetTicksNS();
    if (!value->render_dirty && value->last_render_ns) return;
    value->render_dirty = false;
    value->last_render_ns = now;
    bongo_cat_neo_preferences_input_end(value);
    SDL_GL_MakeCurrent(value->window, value->gl_context);
    bongo_cat_neo_preferences_apply_theme(value);
    int width, height;
    SDL_GetWindowSize(value->window, &width, &height);
    bongo_cat_neo_ui_cursor_begin(&value->ui);
    bool dark = bongo_cat_neo_preferences_resolved_theme(value) != 0;
    bool close_requested = draw_frame(value, width, height, dark);
    BongoCatNeoUIPalette palette = bongo_cat_neo_ui_palette(dark);
    glClearColor(palette.background.r / 255.0f, palette.background.g / 255.0f,
        palette.background.b / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    bongo_cat_neo_ui_render(&value->ui);
    SDL_GL_SwapWindow(value->window);
    write_smoke_frame(value);
    SDL_GL_MakeCurrent(value->app->window, value->app->gl_context);
    bongo_cat_neo_ui_cursor_apply(&value->ui);
    if (close_requested) {
        bongo_cat_neo_preferences_close(value);
        return;
    }
    if (value->import_requested && !bongo_cat_neo_preferences_import_is_open(
        value->import_dialog)) {
        value->import_requested = false;
        if (!bongo_cat_neo_preferences_import_open(value->import_dialog, value->window))
            value->render_dirty = true;
    }
    reload_language(value);
    if (value->shortcut_recording ||
        bongo_cat_neo_pref_controls_animating(&value->ui.context) ||
        bongo_cat_neo_ui_animations_active(&value->ui.context))
        value->render_dirty = true;
}
