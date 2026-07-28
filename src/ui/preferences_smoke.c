#include "preferences_state.h"

static void send_key(BongoCatNeoPreferences *value, Uint32 type, bool down) {
    SDL_Event event = {0};
    event.type = type;
    event.key.windowID = SDL_GetWindowID(value->window);
    event.key.down = down;
    event.key.scancode = SDL_SCANCODE_B;
    event.key.key = SDLK_B;
    event.key.mod = SDL_KMOD_CTRL | SDL_KMOD_SHIFT;
    bongo_cat_neo_preferences_event(value, &event);
}

void bongo_cat_neo_preferences_shortcut_smoke(BongoCatNeoPreferences *value) {
    if (!value || !value->window || !value->app->smoke_preference_shortcut ||
        !value->shortcut_recording) return;
    value->app->smoke_preference_shortcut = false;
    send_key(value, SDL_EVENT_KEY_DOWN, true);
    send_key(value, SDL_EVENT_KEY_UP, false);
}
