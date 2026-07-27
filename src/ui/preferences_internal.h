#ifndef BONGO_CAT_NEO_PREFERENCES_INTERNAL_H
#define BONGO_CAT_NEO_PREFERENCES_INTERNAL_H

#include "bongo_cat_neo/app.h"
#include "nuklear_config.h"
#include <SDL3/SDL.h>

void bongo_cat_neo_preferences_page_cat(BongoCatNeoApp *app, struct nk_context *context);
void bongo_cat_neo_preferences_page_general(BongoCatNeoApp *app, struct nk_context *context);
void bongo_cat_neo_preferences_page_model(BongoCatNeoPreferences *value,
    struct nk_context *context);
bool bongo_cat_neo_preferences_behavior_dialog_active(
    const BongoCatNeoPreferences *value);
void bongo_cat_neo_preferences_behavior_dialog_open(
    BongoCatNeoPreferences *value);
void bongo_cat_neo_preferences_behavior_dialog_draw(
    BongoCatNeoPreferences *value, struct nk_context *context);
void bongo_cat_neo_preferences_behavior_dialog_close(
    BongoCatNeoPreferences *value);
void bongo_cat_neo_preferences_page_shortcuts(BongoCatNeoPreferences *value,
    struct nk_context *context);
void bongo_cat_neo_preferences_page_about(BongoCatNeoPreferences *value,
    struct nk_context *context);
bool bongo_cat_neo_preferences_shortcut_active(const BongoCatNeoPreferences *value,
    const char *id);
void bongo_cat_neo_preferences_shortcut_begin(BongoCatNeoPreferences *value,
    const char *id, char *target, int capacity);
bool bongo_cat_neo_preferences_shortcut_event(BongoCatNeoPreferences *value,
    const SDL_Event *event);
void bongo_cat_neo_preferences_shortcut_cancel(BongoCatNeoPreferences *value);
void bongo_cat_neo_preferences_import_path(BongoCatNeoApp *app, SDL_Window *window,
    const char *path);
void bongo_cat_neo_preferences_model_cache_clear(BongoCatNeoApp *app);
bool bongo_cat_neo_preferences_remove_dialog_active(const BongoCatNeoApp *app);
void bongo_cat_neo_preferences_remove_dialog_open(BongoCatNeoApp *app, const char *id);
void bongo_cat_neo_preferences_remove_dialog_draw(BongoCatNeoApp *app,
    struct nk_context *context);
void bongo_cat_neo_preferences_remove_dialog_clear(const BongoCatNeoApp *app);
void bongo_cat_neo_preferences_remove_dialog_close(BongoCatNeoApp *app);

typedef struct BongoCatNeoImportDialog BongoCatNeoImportDialog;
BongoCatNeoImportDialog *bongo_cat_neo_preferences_import_create(void);
void bongo_cat_neo_preferences_import_destroy(BongoCatNeoImportDialog *dialog);
bool bongo_cat_neo_preferences_import_open(BongoCatNeoImportDialog *dialog,
    SDL_Window *window);
bool bongo_cat_neo_preferences_import_is_open(const BongoCatNeoImportDialog *dialog);
bool bongo_cat_neo_preferences_import_event(BongoCatNeoImportDialog *dialog,
    BongoCatNeoApp *app, SDL_Window *window, const SDL_Event *event);

#endif
