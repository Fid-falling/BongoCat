#include "preferences_state.h"
#include "preferences_text_edit.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

bool bongo_cat_preferences_model_name_draw(BongoCatPreferences *value,
    struct nk_context *context, struct nk_command_buffer *canvas,
    const BongoCatModelEntry *entry, struct nk_rect bounds,
    BongoCatUIPalette p) {
    bool renaming = !strcmp(value->model_rename_id, entry->id);
    bool hover = !renaming && nk_input_is_mouse_hovering_rect(
        &context->input, bounds);
    bool clicked = hover && nk_input_is_mouse_click_in_rect(
        &context->input, NK_BUTTON_LEFT, bounds);
    if (renaming || hover) {
        nk_fill_rect(canvas, bounds, 7, renaming ? p.hover_pink : p.hover);
        nk_stroke_rect(canvas, bounds, 7, 1, renaming ? p.pink : p.accent);
    }
    const char *shown = renaming ? value->model_rename_text :
        bongo_cat_model_name(&value->app->config, entry);
    struct nk_rect text_bounds = nk_rect(bounds.x + 5,
        bounds.y + 3, bounds.w - 10, 22);
    if (renaming && value->model_rename_select_all)
        nk_fill_rect(canvas, text_bounds, 4, p.selection);
    nk_draw_text(canvas, text_bounds, shown, nk_strlen(shown), value->ui.label_font,
        nk_rgba(0, 0, 0, 0), renaming ? p.pink : p.text);
    if (renaming) {
        value->model_rename_bounds = bounds;
        float caret = value->ui.label_font->width(
            value->ui.label_font->userdata, value->ui.label_font->height,
            shown, (int)value->model_rename_cursor);
        caret = NK_MIN(caret, bounds.w - 12);
        if (!value->model_rename_select_all)
            nk_stroke_line(canvas, bounds.x + 5 + caret, bounds.y + 3,
                bounds.x + 5 + caret, bounds.y + bounds.h - 3, 1, p.pink);
    } else if (clicked)
        bongo_cat_preferences_model_rename_begin(value, entry, bounds);
    if (hover || renaming) bongo_cat_ui_cursor_hover_rect(context, bounds,
        BONGO_CAT_UI_CURSOR_TEXT);
    return renaming || hover;
}

static void clipboard_event(BongoCatPreferences *value, SDL_Keycode key) {
    if (key == SDLK_V) {
        char *clipboard = SDL_GetClipboardText();
        bongo_cat_text_edit_insert(value->model_rename_text,
            sizeof(value->model_rename_text), &value->model_rename_cursor,
            &value->model_rename_select_all, clipboard);
        SDL_free(clipboard);
        return;
    }
    if (value->model_rename_select_all)
        SDL_SetClipboardText(value->model_rename_text);
    if (key == SDLK_X && value->model_rename_select_all)
        bongo_cat_text_edit_clear(value->model_rename_text,
            &value->model_rename_cursor, &value->model_rename_select_all);
}

static float measure(const void *userdata, const char *text, size_t length) {
    const struct nk_user_font *font = userdata;
    return font->width(font->userdata, font->height, text, (int)length);
}

void bongo_cat_preferences_model_rename_finish(
    BongoCatPreferences *value, bool save) {
    if (!value || !value->model_rename_id[0]) return;
    bool label_changed = false;
    if (save) {
        size_t start = 0, end = strlen(value->model_rename_text);
        while (start < end && (value->model_rename_text[start] == ' ' ||
            value->model_rename_text[start] == '\t')) start++;
        while (end > start && (value->model_rename_text[end - 1] == ' ' ||
            value->model_rename_text[end - 1] == '\t')) end--;
        if (start) memmove(value->model_rename_text,
            value->model_rename_text + start, end - start);
        value->model_rename_text[end - start] = '\0';
        const BongoCatModelEntry *entry = bongo_cat_models_find(
            &value->app->models, value->model_rename_id);
        const char *label = entry && !strcmp(value->model_rename_text,
            bongo_cat_model_default_name(entry)) ? "" :
            value->model_rename_text;
        label_changed = bongo_cat_config_set_model_label(&value->app->config,
            value->model_rename_id, label);
    }
    value->model_rename_id[0] = '\0';
    value->model_rename_text[0] = '\0';
    value->model_rename_cursor = 0;
    value->model_rename_select_all = false;
    SDL_StopTextInput(value->window);
    if (label_changed) bongo_cat_preferences_reload_fonts(value);
    value->render_dirty = true;
}

void bongo_cat_preferences_model_rename_begin(BongoCatPreferences *value,
    const BongoCatModelEntry *entry, struct nk_rect bounds) {
    if (!value || !entry) return;
    bongo_cat_preferences_shortcut_cancel(value);
    snprintf(value->model_rename_id, sizeof(value->model_rename_id),
        "%s", entry->id);
    snprintf(value->model_rename_text, sizeof(value->model_rename_text),
        "%s", bongo_cat_model_name(&value->app->config, entry));
    value->model_rename_bounds = bounds;
    bongo_cat_text_edit_begin(value->model_rename_text,
        &value->model_rename_cursor, &value->model_rename_select_all);
    SDL_StartTextInput(value->window);
    value->render_dirty = true;
}

bool bongo_cat_preferences_model_rename_event(
    BongoCatPreferences *value, const SDL_Event *event) {
    if (!value || !event || !value->model_rename_id[0]) return false;
    if (event->type == SDL_EVENT_TEXT_INPUT) {
        bongo_cat_text_edit_insert(value->model_rename_text,
            sizeof(value->model_rename_text), &value->model_rename_cursor,
            &value->model_rename_select_all, event->text.text);
        value->render_dirty = true; return true;
    }
    if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        bongo_cat_preferences_model_rename_finish(value, true); return false;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event->button.button == SDL_BUTTON_LEFT) {
        float scale = value->ui.layout_scale > 0.0f ?
            value->ui.layout_scale : 1.0f;
        float x = event->button.x / scale, y = event->button.y / scale;
        if (!NK_INBOX(x, y, value->model_rename_bounds.x,
            value->model_rename_bounds.y, value->model_rename_bounds.w,
            value->model_rename_bounds.h))
            bongo_cat_preferences_model_rename_finish(value, true);
        else {
            value->model_rename_cursor = bongo_cat_text_edit_nearest(
                value->model_rename_text,
                x - value->model_rename_bounds.x - 5.0f, measure,
                value->ui.label_font);
            value->model_rename_select_all = false;
            value->render_dirty = true;
            return true;
        }
        return false;
    }
    if (event->type != SDL_EVENT_KEY_DOWN &&
        event->type != SDL_EVENT_KEY_UP) return false;
    if (event->type == SDL_EVENT_KEY_UP) return true;
    SDL_Keycode key = event->key.key;
    bool control = (event->key.mod & SDL_KMOD_CTRL) != 0;
    if (key == SDLK_ESCAPE)
        bongo_cat_preferences_model_rename_finish(value, false);
    else if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
        bongo_cat_preferences_model_rename_finish(value, true);
    else if (control && key == SDLK_A)
        bongo_cat_text_edit_select_all(value->model_rename_text,
            &value->model_rename_cursor, &value->model_rename_select_all);
    else if (control && (key == SDLK_C || key == SDLK_X || key == SDLK_V))
        clipboard_event(value, key);
    else if (key == SDLK_BACKSPACE || key == SDLK_DELETE)
        bongo_cat_text_edit_erase(value->model_rename_text,
            &value->model_rename_cursor, &value->model_rename_select_all,
            key == SDLK_DELETE);
    else if (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_HOME ||
        key == SDLK_END)
        bongo_cat_text_edit_move(value->model_rename_text,
            &value->model_rename_cursor, &value->model_rename_select_all,
            key == SDLK_LEFT ? BONGO_CAT_TEXT_EDIT_LEFT :
            key == SDLK_RIGHT ? BONGO_CAT_TEXT_EDIT_RIGHT :
            key == SDLK_HOME ? BONGO_CAT_TEXT_EDIT_HOME : BONGO_CAT_TEXT_EDIT_END);
    value->render_dirty = true;
    return true;
}
