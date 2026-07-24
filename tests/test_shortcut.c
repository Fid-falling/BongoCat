#include "test.h"
#include "bongo_cat_neo/shortcut.h"

#include <string.h>

static BongoCatNeoInputEvent key(BongoCatNeoInputKind kind, const char *name) {
    BongoCatNeoInputEvent event = {.kind = kind};
    snprintf(event.name, sizeof(event.name), "%s", name);
    return event;
}

void test_shortcut(void) {
    BongoCatNeoShortcutState state;
    bongo_cat_neo_shortcut_init(&state);
    BongoCatNeoInputEvent control = key(BONGO_CAT_NEO_INPUT_KEY_DOWN, "ControlLeft");
    BongoCatNeoInputEvent letter = key(BONGO_CAT_NEO_INPUT_KEY_DOWN, "KeyB");
    CHECK(!bongo_cat_neo_shortcut_update(&state, &control));
    CHECK(bongo_cat_neo_shortcut_update(&state, &letter));
    CHECK(bongo_cat_neo_shortcut_matches(&state, &letter, "Control+B"));
    CHECK(!bongo_cat_neo_shortcut_matches(&state, &letter, "Control+Shift+B"));
    CHECK(!bongo_cat_neo_shortcut_update(&state, &letter));
    BongoCatNeoInputEvent up = key(BONGO_CAT_NEO_INPUT_KEY_UP, "KeyB");
    bongo_cat_neo_shortcut_update(&state, &up);
    control.kind = BONGO_CAT_NEO_INPUT_KEY_UP;
    bongo_cat_neo_shortcut_update(&state, &control);
    BongoCatNeoInputEvent function = key(BONGO_CAT_NEO_INPUT_KEY_DOWN, "F1");
    CHECK(bongo_cat_neo_shortcut_update(&state, &function));
    CHECK(bongo_cat_neo_shortcut_matches(&state, &function, "F1"));
    BongoCatNeoInputEvent comma = key(BONGO_CAT_NEO_INPUT_KEY_DOWN, "Comma");
    up = key(BONGO_CAT_NEO_INPUT_KEY_UP, "F1");
    bongo_cat_neo_shortcut_update(&state, &up);
    control.kind = BONGO_CAT_NEO_INPUT_KEY_DOWN;
    bongo_cat_neo_shortcut_update(&state, &control);
    CHECK(bongo_cat_neo_shortcut_update(&state, &comma));
    CHECK(bongo_cat_neo_shortcut_matches(&state, &comma, "Control+Comma"));
    up = key(BONGO_CAT_NEO_INPUT_KEY_UP, "Comma");
    bongo_cat_neo_shortcut_update(&state, &up);
    control.kind = BONGO_CAT_NEO_INPUT_KEY_UP;
    bongo_cat_neo_shortcut_update(&state, &control);
    BongoCatNeoInputEvent alt = key(BONGO_CAT_NEO_INPUT_KEY_DOWN, "Alt");
    BongoCatNeoInputEvent digit = key(BONGO_CAT_NEO_INPUT_KEY_DOWN, "Num1");
    CHECK(!bongo_cat_neo_shortcut_update(&state, &alt));
    CHECK(bongo_cat_neo_shortcut_update(&state, &digit));
    CHECK(bongo_cat_neo_shortcut_matches(&state, &digit, "Alt+1"));

    BongoCatNeoInputEvent gamepad = {
        .kind = BONGO_CAT_NEO_INPUT_GAMEPAD_BUTTON, .value = 1.0f
    };
    snprintf(gamepad.name, sizeof(gamepad.name), "South");
    CHECK(bongo_cat_neo_shortcut_matches(&state, &gamepad, "Gamepad:South"));
    CHECK(!bongo_cat_neo_shortcut_matches(&state, &gamepad, "South"));
    gamepad.value = 0.0f;
    CHECK(!bongo_cat_neo_shortcut_matches(&state, &gamepad, "Gamepad:South"));
}
