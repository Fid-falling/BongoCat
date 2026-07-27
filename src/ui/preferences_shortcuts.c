#include "preferences_state.h"
#include "preferences_widgets.h"
#include "bongo_cat_neo/i18n.h"
#include "bongo_cat_neo/shortcut.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static const char *tr(const BongoCatNeoPreferences *value, const char *key,
    const char *fallback) {
    return bongo_cat_neo_i18n_get(value->app->i18n, key, fallback);
}

bool bongo_cat_neo_preferences_shortcut_active(const BongoCatNeoPreferences *value,
    const char *id) {
    return value && value->shortcut_recording && value->shortcut_id[0] && id &&
        strcmp(value->shortcut_id, id) == 0;
}

static void finish(BongoCatNeoPreferences *value) {
    value->shortcut_recording = false;
    value->shortcut_id[0] = '\0';
    value->shortcut_target = NULL;
    value->shortcut_capacity = 0;
    value->shortcut_key = SDLK_UNKNOWN;
    value->shortcut_suppress_until_ns = SDL_GetTicksNS() + 250000000ULL;
    bongo_cat_neo_shortcut_init(&value->app->shortcut_state);
    value->render_dirty = true;
}

void bongo_cat_neo_preferences_shortcut_cancel(BongoCatNeoPreferences *value) {
    if (!value || !value->shortcut_recording) return;
    if (value->shortcut_target && value->shortcut_capacity > 0)
        snprintf(value->shortcut_target, (size_t)value->shortcut_capacity, "%s",
            value->shortcut_original);
    finish(value);
}

void bongo_cat_neo_preferences_shortcut_begin(BongoCatNeoPreferences *value,
    const char *id, char *target, int capacity) {
    if (!value || !id || !target || capacity < 1) return;
    if (bongo_cat_neo_preferences_shortcut_active(value, id)) return;
    bongo_cat_neo_preferences_shortcut_cancel(value);
    snprintf(value->shortcut_id, sizeof(value->shortcut_id), "%s", id);
    value->shortcut_target = target;
    value->shortcut_capacity = capacity;
    snprintf(value->shortcut_original, sizeof(value->shortcut_original), "%s", target);
    value->shortcut_key = SDLK_UNKNOWN;
    value->shortcut_recording = true;
    bongo_cat_neo_shortcut_init(&value->app->shortcut_state);
    value->render_dirty = true;
}

static bool modifier_key(SDL_Keycode key) {
    return key == SDLK_LCTRL || key == SDLK_RCTRL ||
        key == SDLK_LSHIFT || key == SDLK_RSHIFT ||
        key == SDLK_LALT || key == SDLK_RALT ||
        key == SDLK_LGUI || key == SDLK_RGUI;
}

static const char *primary_name(SDL_Keycode key, char output[24]) {
    if (key >= SDLK_A && key <= SDLK_Z) {
        output[0] = (char)('A' + key - SDLK_A); output[1] = '\0'; return output;
    }
    if (key >= SDLK_0 && key <= SDLK_9) {
        output[0] = (char)('0' + key - SDLK_0); output[1] = '\0'; return output;
    }
    if (key >= SDLK_F1 && key <= SDLK_F24) {
        snprintf(output, 24, "F%d", (int)(key - SDLK_F1 + 1)); return output;
    }
    switch (key) {
    case SDLK_RETURN: case SDLK_KP_ENTER: return "Enter";
    case SDLK_TAB: return "Tab"; case SDLK_SPACE: return "Space";
    case SDLK_PAUSE: return "Pause"; case SDLK_PRINTSCREEN: return "PrintScreen";
    case SDLK_INSERT: return "Insert"; case SDLK_HOME: return "Home";
    case SDLK_END: return "End"; case SDLK_PAGEUP: return "PageUp";
    case SDLK_PAGEDOWN: return "PageDown"; case SDLK_UP: return "ArrowUp";
    case SDLK_DOWN: return "ArrowDown"; case SDLK_LEFT: return "ArrowLeft";
    case SDLK_RIGHT: return "ArrowRight"; case SDLK_CAPSLOCK: return "CapsLock";
    case SDLK_NUMLOCKCLEAR: return "NumLock"; case SDLK_SCROLLLOCK: return "ScrollLock";
    case SDLK_GRAVE: return "BackQuote"; case SDLK_MINUS: return "-";
    case SDLK_EQUALS: return "="; case SDLK_LEFTBRACKET: return "BracketLeft";
    case SDLK_RIGHTBRACKET: return "BracketRight"; case SDLK_BACKSLASH: return "Backslash";
    case SDLK_SEMICOLON: return "Semicolon"; case SDLK_APOSTROPHE: return "Quote";
    case SDLK_COMMA: return "Comma"; case SDLK_PERIOD: return "Period";
    case SDLK_SLASH: return "Slash";
    case SDLK_KP_0: return "Kp0"; case SDLK_KP_1: return "Kp1";
    case SDLK_KP_2: return "Kp2"; case SDLK_KP_3: return "Kp3";
    case SDLK_KP_4: return "Kp4"; case SDLK_KP_5: return "Kp5";
    case SDLK_KP_6: return "Kp6"; case SDLK_KP_7: return "Kp7";
    case SDLK_KP_8: return "Kp8"; case SDLK_KP_9: return "Kp9";
    case SDLK_KP_MULTIPLY: return "KpMultiply"; case SDLK_KP_PLUS: return "KpPlus";
    case SDLK_KP_MINUS: return "KpMinus"; case SDLK_KP_DECIMAL: return "KpDecimal";
    case SDLK_KP_DIVIDE: return "KpDivide"; default: return NULL;
    }
}

static void append(char *output, size_t capacity, const char *token) {
    size_t length = strlen(output);
    if (length >= capacity) return;
    snprintf(output + length, capacity - length, "%s%s", length ? "+" : "", token);
}

static bool capture_key(BongoCatNeoPreferences *value,
    const SDL_KeyboardEvent *event) {
    if (event->repeat) return true;
    if (!event->down) {
        if (value->shortcut_key != SDLK_UNKNOWN && event->key == value->shortcut_key)
            finish(value);
        return true;
    }
    if (event->key == SDLK_ESCAPE) {
        bongo_cat_neo_preferences_shortcut_cancel(value); return true;
    }
    if (event->key == SDLK_BACKSPACE || event->key == SDLK_DELETE) {
        value->shortcut_target[0] = '\0'; value->shortcut_key = event->key;
        value->render_dirty = true; return true;
    }
    if (modifier_key(event->key)) return true;
    char primary[24]; const char *key = primary_name(event->key, primary);
    if (!key) return true;
    char shortcut[BONGO_CAT_NEO_SHORTCUT_CAP] = {0};
    if (event->mod & SDL_KMOD_CTRL) append(shortcut, sizeof(shortcut), "Control");
    if (event->mod & SDL_KMOD_SHIFT) append(shortcut, sizeof(shortcut), "Shift");
    if (event->mod & SDL_KMOD_ALT) append(shortcut, sizeof(shortcut), "Alt");
    if (event->mod & SDL_KMOD_GUI) append(shortcut, sizeof(shortcut), "Meta");
    append(shortcut, sizeof(shortcut), key);
    snprintf(value->shortcut_target, (size_t)value->shortcut_capacity, "%s", shortcut);
    value->shortcut_key = event->key;
    value->render_dirty = true;
    return true;
}

bool bongo_cat_neo_preferences_shortcut_event(BongoCatNeoPreferences *value,
    const SDL_Event *event) {
    if (!value || !event || !value->shortcut_recording) return false;
    if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP)
        return capture_key(value, &event->key);
    if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        bongo_cat_neo_preferences_shortcut_cancel(value); return false;
    }
    return false;
}

bool bongo_cat_neo_preferences_shortcuts_blocked(
    const BongoCatNeoPreferences *value) {
    return value && (value->shortcut_recording ||
        SDL_GetTicksNS() < value->shortcut_suppress_until_ns);
}

static void shortcut_row(BongoCatNeoPreferences *value, struct nk_context *context,
    const char *id, const char *label_key, const char *label_fallback,
    char *target, int capacity) {
    bool active = bongo_cat_neo_preferences_shortcut_active(value, id);
    int action = bongo_cat_neo_pref_edit(context, id,
        tr(value, label_key, label_fallback), "",
        target, active, tr(value, "components.shortcut.hints.clickRecordShortcut",
        "Click to record shortcut"), tr(value,
        "components.shortcut.hints.pressRecordShortcut", "Press shortcut"));
    if (action < 0) {
        target[0] = '\0'; value->render_dirty = true;
    } else if (action > 0) {
        bongo_cat_neo_preferences_shortcut_begin(value, id, target, capacity);
    }
}

void bongo_cat_neo_preferences_page_shortcuts(BongoCatNeoPreferences *value,
    struct nk_context *context) {
    BongoCatNeoShortcutOptions *keys = &value->app->config.shortcuts;
    bongo_cat_neo_pref_section(context, tr(value,
        "pages.preference.shortcut.title", "Shortcuts"));
    bool zh = value->app->config.app.language == BONGO_CAT_NEO_LANG_ZH_CN;
    shortcut_row(value, context, "shortcut-cat", zh ? "native.shortcut.toggleCat" :
        "pages.preference.shortcut.labels.toggleCat", "Toggle Cat",
        keys->visible_cat, sizeof(keys->visible_cat));
    shortcut_row(value, context, "shortcut-pref", zh ? "native.shortcut.preferences" :
        "pages.preference.shortcut.labels.togglePreferences", "Toggle Preferences",
        keys->visible_preferences, sizeof(keys->visible_preferences));
    shortcut_row(value, context, "shortcut-mirror", zh ? "native.shortcut.mirror" :
        "pages.preference.shortcut.labels.mirrorMode", "Mirror Mode",
        keys->mirror, sizeof(keys->mirror));
    shortcut_row(value, context, "shortcut-pass", zh ? "native.shortcut.passThrough" :
        "pages.preference.shortcut.labels.passThrough", "Pass Through",
        keys->pass_through, sizeof(keys->pass_through));
    shortcut_row(value, context, "shortcut-top", zh ? "native.shortcut.alwaysOnTop" :
        "pages.preference.shortcut.labels.alwaysOnTop", "Always on Top",
        keys->always_on_top, sizeof(keys->always_on_top));
}
