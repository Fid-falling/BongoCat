#include "ui_backend.h"

void bongo_cat_ui_input_begin(BongoCatUIBackend *ui) {
    if (ui) nk_input_begin(&ui->context);
}

void bongo_cat_ui_input_end(BongoCatUIBackend *ui) {
    if (ui) nk_input_end(&ui->context);
}

static void key_event(BongoCatUIBackend *ui, const SDL_KeyboardEvent *event) {
    bool down = event->down;
    bool control = (event->mod & SDL_KMOD_CTRL) != 0;
    struct nk_context *context = &ui->context;
    switch (event->key) {
    case SDLK_LSHIFT: case SDLK_RSHIFT: nk_input_key(context, NK_KEY_SHIFT, down); break;
    case SDLK_LALT: case SDLK_RALT: nk_input_key(context, NK_KEY_ALT, down); break;
    case SDLK_DELETE: nk_input_key(context, NK_KEY_DEL, down); break;
    case SDLK_RETURN: case SDLK_KP_ENTER: nk_input_key(context, NK_KEY_ENTER, down); break;
    case SDLK_TAB: nk_input_key(context, NK_KEY_TAB, down); break;
    case SDLK_BACKSPACE: nk_input_key(context, NK_KEY_BACKSPACE, down); break;
    case SDLK_HOME:
        nk_input_key(context, NK_KEY_TEXT_START, down);
        nk_input_key(context, NK_KEY_SCROLL_START, down);
        break;
    case SDLK_END:
        nk_input_key(context, NK_KEY_TEXT_END, down);
        nk_input_key(context, NK_KEY_SCROLL_END, down);
        break;
    case SDLK_PAGEUP: nk_input_key(context, NK_KEY_SCROLL_UP, down); break;
    case SDLK_PAGEDOWN: nk_input_key(context, NK_KEY_SCROLL_DOWN, down); break;
    case SDLK_UP: nk_input_key(context, NK_KEY_UP, down); break;
    case SDLK_DOWN: nk_input_key(context, NK_KEY_DOWN, down); break;
    case SDLK_LEFT:
        nk_input_key(context, control ? NK_KEY_TEXT_WORD_LEFT : NK_KEY_LEFT, down);
        break;
    case SDLK_RIGHT:
        nk_input_key(context, control ? NK_KEY_TEXT_WORD_RIGHT : NK_KEY_RIGHT, down);
        break;
    case SDLK_A: if (control) nk_input_key(context, NK_KEY_TEXT_SELECT_ALL, down); break;
    case SDLK_C: if (control) nk_input_key(context, NK_KEY_COPY, down); break;
    case SDLK_V: if (control) nk_input_key(context, NK_KEY_PASTE, down); break;
    case SDLK_X: if (control) nk_input_key(context, NK_KEY_CUT, down); break;
    case SDLK_Z: if (control) nk_input_key(context, NK_KEY_TEXT_UNDO, down); break;
    default: break;
    }
}

static void text_event(BongoCatUIBackend *ui, const char *text) {
    int length = text ? (int)SDL_strlen(text) : 0;
    while (length > 0) {
        nk_rune rune;
        int consumed = nk_utf_decode(text, &rune, length);
        if (consumed <= 0) break;
        nk_input_unicode(&ui->context, rune);
        text += consumed;
        length -= consumed;
    }
}

bool bongo_cat_ui_event(BongoCatUIBackend *ui, const SDL_Event *event) {
    if (!ui || !event) return false;
    struct nk_context *context = &ui->context;
    float scale = ui->layout_scale > 0.0f ? ui->layout_scale : 1.0f;
    switch (event->type) {
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP:
        key_event(ui, &event->key);
        return true;
    case SDL_EVENT_TEXT_INPUT:
        text_event(ui, event->text.text);
        return true;
    case SDL_EVENT_MOUSE_MOTION:
        nk_input_motion(context, (int)(event->motion.x / scale),
            (int)(event->motion.y / scale));
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: {
        bool down = event->button.down;
        enum nk_buttons button = event->button.button == SDL_BUTTON_LEFT ? NK_BUTTON_LEFT :
            event->button.button == SDL_BUTTON_MIDDLE ? NK_BUTTON_MIDDLE : NK_BUTTON_RIGHT;
        nk_input_button(context, button, (int)(event->button.x / scale),
            (int)(event->button.y / scale), down);
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        nk_input_scroll(context, nk_vec2(event->wheel.x, event->wheel.y));
        return true;
    default: return false;
    }
}
