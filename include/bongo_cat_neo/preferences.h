#ifndef BONGO_CAT_NEO_PREFERENCES_H
#define BONGO_CAT_NEO_PREFERENCES_H

#include "bongo_cat_neo/common.h"

typedef union SDL_Event SDL_Event;
typedef struct BongoCatNeoApp BongoCatNeoApp;
typedef struct BongoCatNeoPreferences BongoCatNeoPreferences;

BongoCatNeoPreferences *bongo_cat_neo_preferences_create(BongoCatNeoApp *app);
void bongo_cat_neo_preferences_destroy(BongoCatNeoPreferences *preferences);
void bongo_cat_neo_preferences_show(BongoCatNeoPreferences *preferences);
void bongo_cat_neo_preferences_close(BongoCatNeoPreferences *preferences);
bool bongo_cat_neo_preferences_visible(const BongoCatNeoPreferences *preferences);
bool bongo_cat_neo_preferences_needs_frame(const BongoCatNeoPreferences *preferences);
void bongo_cat_neo_preferences_input_begin(BongoCatNeoPreferences *preferences);
void bongo_cat_neo_preferences_input_end(BongoCatNeoPreferences *preferences);
bool bongo_cat_neo_preferences_event(BongoCatNeoPreferences *preferences, const SDL_Event *event);
bool bongo_cat_neo_preferences_shortcuts_blocked(
    const BongoCatNeoPreferences *preferences);
void bongo_cat_neo_preferences_render(BongoCatNeoPreferences *preferences);
void bongo_cat_neo_preferences_invalidate(BongoCatNeoPreferences *preferences);
void bongo_cat_neo_preferences_request_model_import(BongoCatNeoPreferences *preferences);

#endif
