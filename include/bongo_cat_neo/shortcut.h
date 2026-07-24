#ifndef BONGO_CAT_NEO_SHORTCUT_H
#define BONGO_CAT_NEO_SHORTCUT_H

#include "bongo_cat_neo/input.h"

typedef struct BongoCatNeoShortcutState {
    uint8_t control;
    uint8_t shift;
    uint8_t alt;
    uint8_t meta;
    char pressed[BONGO_CAT_NEO_ID_CAP];
} BongoCatNeoShortcutState;

void bongo_cat_neo_shortcut_init(BongoCatNeoShortcutState *state);
bool bongo_cat_neo_shortcut_update(BongoCatNeoShortcutState *state, const BongoCatNeoInputEvent *event);
bool bongo_cat_neo_shortcut_matches(const BongoCatNeoShortcutState *state,
    const BongoCatNeoInputEvent *event, const char *shortcut);
bool bongo_cat_neo_shortcut_release_matches(const BongoCatNeoInputEvent *event,
    const char *shortcut);

#endif
