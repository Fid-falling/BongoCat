#ifndef BONGO_CAT_NEO_PREFERENCES_SHORTCUT_CLEAR_H
#define BONGO_CAT_NEO_PREFERENCES_SHORTCUT_CLEAR_H

#include "ui_catime.h"

bool bongo_cat_neo_pref_shortcut_clear(struct nk_context *context,
    struct nk_command_buffer *canvas, const char *id, struct nk_rect bounds,
    BongoCatNeoUIPalette palette, float opacity, bool enabled);

#endif
