#include "bongo_cat_neo/tray.h"
#include "bongo_cat_neo/app.h"
#include "bongo_cat_neo/image.h"
#include "bongo_cat_neo/i18n.h"
#include "bongo_cat_neo/path.h"
#include "bongo_cat_neo/preferences.h"
#include "runtime.h"

#include <SDL3/SDL.h>
#include <stdlib.h>

struct BongoCatNeoTray {
    BongoCatNeoApp *app;
    SDL_Tray *handle;
    SDL_TrayEntry *visible;
    SDL_TrayEntry *pass_through;
    SDL_TrayEntry *always_on_top;
    SDL_TrayEntry *preferences;
    SDL_TrayEntry *exit;
    BongoCatNeoImage icon;
    bool state_valid;
    bool last_visible, last_pass_through, last_always_on_top;
    BongoCatNeoLanguage last_language;
};

static void on_visible(void *userdata, SDL_TrayEntry *entry) {
    (void)entry;
    BongoCatNeoTray *tray = userdata;
    BongoCatNeoApp *app = tray->app;
    bongo_cat_neo_window_set_visible(app, !app->config.window.visible);
    bongo_cat_neo_tray_sync(tray);
    bongo_cat_neo_preferences_invalidate(app->preferences);
}

static void on_pass_through(void *userdata, SDL_TrayEntry *entry) {
    (void)entry;
    BongoCatNeoTray *tray = userdata;
    tray->app->config.window.pass_through = !tray->app->config.window.pass_through;
    bongo_cat_neo_window_mark_hit_dirty(tray->app);
    bongo_cat_neo_window_sync_click_through(tray->app);
    bongo_cat_neo_tray_sync(tray);
    bongo_cat_neo_preferences_invalidate(tray->app->preferences);
}

static void on_always_on_top(void *userdata, SDL_TrayEntry *entry) {
    (void)entry;
    BongoCatNeoTray *tray = userdata;
    tray->app->config.window.always_on_top = !tray->app->config.window.always_on_top;
    bongo_cat_neo_platform_set_always_on_top(&tray->app->platform,
        tray->app->config.window.always_on_top);
    bongo_cat_neo_tray_sync(tray);
    bongo_cat_neo_preferences_invalidate(tray->app->preferences);
}

static void on_preferences(void *userdata, SDL_TrayEntry *entry) {
    (void)entry;
    BongoCatNeoTray *tray = userdata;
    bongo_cat_neo_preferences_show(tray->app->preferences);
}

static void on_tray_left_click(void *userdata) {
    BongoCatNeoTray *tray = userdata;
    if (tray && tray->app && tray->app->preferences)
        bongo_cat_neo_preferences_show(tray->app->preferences);
}

static void on_exit(void *userdata, SDL_TrayEntry *entry) {
    (void)entry;
    ((BongoCatNeoTray *)userdata)->app->running = false;
}

static SDL_TrayEntry *add(SDL_TrayMenu *menu, const char *label,
    SDL_TrayEntryFlags flags, SDL_TrayCallback callback, BongoCatNeoTray *tray) {
    SDL_TrayEntry *entry = SDL_InsertTrayEntryAt(menu, -1, label, flags);
    if (entry && callback) SDL_SetTrayEntryCallback(entry, callback, tray);
    return entry;
}

BongoCatNeoTray *bongo_cat_neo_tray_create(BongoCatNeoApp *app, BongoCatNeoError *error) {
    if (!app || !app->config.app.tray_visible) return NULL;
    BongoCatNeoTray *tray = calloc(1, sizeof(*tray));
    if (!tray) return NULL;
    tray->app = app;
    char path[BONGO_CAT_NEO_PATH_CAP];
    bongo_cat_neo_path_join(path, sizeof(path), app->asset_root,
#ifdef __APPLE__
        "tray-mac.png");
#else
        "tray.png");
#endif
    if (bongo_cat_neo_image_load(path, &tray->icon, error) != BONGO_CAT_NEO_OK) {
        bongo_cat_neo_tray_destroy(tray);
        return NULL;
    }
    tray->handle = SDL_CreateTray(tray->icon.surface, BONGO_CAT_NEO_NAME);
    bongo_cat_neo_image_free(&tray->icon);
    if (!tray->handle) {
        bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_PLATFORM, "Tray creation failed: %s", SDL_GetError());
        bongo_cat_neo_tray_destroy(tray);
        return NULL;
    }
    SDL_TrayMenu *menu = SDL_CreateTrayMenu(tray->handle);
    tray->preferences = add(menu, bongo_cat_neo_i18n_get(app->i18n,
        "composables.useAppMenu.labels.preference", "Preferences"), SDL_TRAYENTRY_BUTTON,
        on_preferences, tray);
    add(menu, NULL, 0, NULL, tray);
    tray->visible = add(menu, bongo_cat_neo_i18n_get(app->i18n,
        "composables.useAppMenu.labels.showCat", "Show Bongo Cat Neo"), SDL_TRAYENTRY_CHECKBOX,
        on_visible, tray);
    tray->pass_through = add(menu, bongo_cat_neo_i18n_get(app->i18n,
        "composables.useAppMenu.labels.passThrough", "Mouse pass-through"), SDL_TRAYENTRY_CHECKBOX,
        on_pass_through, tray);
    tray->always_on_top = add(menu, bongo_cat_neo_i18n_get(app->i18n,
        "composables.useAppMenu.labels.alwaysOnTop", "Always on top"), SDL_TRAYENTRY_CHECKBOX,
        on_always_on_top, tray);
    add(menu, NULL, 0, NULL, tray);
    tray->exit = add(menu, bongo_cat_neo_i18n_get(app->i18n,
        "composables.useAppMenu.labels.quitApp", "Exit"), SDL_TRAYENTRY_BUTTON, on_exit, tray);
    bongo_cat_neo_platform_set_tray_left_click(tray->handle, on_tray_left_click, tray);
    bongo_cat_neo_tray_sync(tray);
    return tray;
}

void bongo_cat_neo_tray_sync(BongoCatNeoTray *tray) {
    if (!tray) return;
    BongoCatNeoApp *app = tray->app;
    if (tray->state_valid && tray->last_visible == app->config.window.visible &&
        tray->last_pass_through == app->config.window.pass_through &&
        tray->last_always_on_top == app->config.window.always_on_top &&
        tray->last_language == app->config.app.language) return;
    tray->state_valid = true;
    tray->last_visible = app->config.window.visible;
    tray->last_pass_through = app->config.window.pass_through;
    tray->last_always_on_top = app->config.window.always_on_top;
    tray->last_language = app->config.app.language;
    SDL_SetTrayEntryChecked(tray->visible, tray->app->config.window.visible);
    SDL_SetTrayEntryChecked(tray->pass_through, tray->app->config.window.pass_through);
    SDL_SetTrayEntryChecked(tray->always_on_top, tray->app->config.window.always_on_top);
    SDL_SetTrayEntryLabel(tray->visible, bongo_cat_neo_i18n_get(tray->app->i18n,
        tray->app->config.window.visible ? "composables.useAppMenu.labels.hideCat" :
        "composables.useAppMenu.labels.showCat", tray->app->config.window.visible
        ? "Hide Bongo Cat Neo" : "Show Bongo Cat Neo"));
    SDL_SetTrayEntryLabel(tray->pass_through, bongo_cat_neo_i18n_get(tray->app->i18n,
        "composables.useAppMenu.labels.passThrough", "Mouse pass-through"));
    SDL_SetTrayEntryLabel(tray->always_on_top, bongo_cat_neo_i18n_get(tray->app->i18n,
        "composables.useAppMenu.labels.alwaysOnTop", "Always on top"));
    SDL_SetTrayEntryLabel(tray->preferences, bongo_cat_neo_i18n_get(tray->app->i18n,
        "composables.useAppMenu.labels.preference", "Preferences"));
    SDL_SetTrayEntryLabel(tray->exit, bongo_cat_neo_i18n_get(tray->app->i18n,
        "composables.useAppMenu.labels.quitApp", "Exit"));
    SDL_UpdateTrays();
}

bool bongo_cat_neo_tray_self_test(BongoCatNeoTray *tray) {
    if (!tray || !tray->handle || !tray->visible || !tray->pass_through ||
        !tray->always_on_top || !tray->preferences || !tray->exit) return false;
    int count = 0;
    const SDL_TrayEntry **entries = SDL_GetTrayEntries(
        SDL_GetTrayEntryParent(tray->preferences), &count);
    if (!entries || count < 1 || entries[0] != tray->preferences) return false;
    BongoCatNeoApp *app = tray->app;
    bool visible = app->config.window.visible;
    bool pass_through = app->config.window.pass_through;
    bool always_on_top = app->config.window.always_on_top;
    on_visible(tray, tray->visible); on_visible(tray, tray->visible);
    on_pass_through(tray, tray->pass_through);
    on_pass_through(tray, tray->pass_through);
    on_always_on_top(tray, tray->always_on_top);
    on_always_on_top(tray, tray->always_on_top);
    on_preferences(tray, tray->preferences);
    bool preferences = bongo_cat_neo_preferences_visible(app->preferences);
    bongo_cat_neo_preferences_close(app->preferences);
    return preferences && app->config.window.visible == visible &&
        app->config.window.pass_through == pass_through &&
        app->config.window.always_on_top == always_on_top;
}

void bongo_cat_neo_tray_destroy(BongoCatNeoTray *tray) {
    if (!tray) return;
    bongo_cat_neo_platform_set_tray_left_click(tray->handle, NULL, NULL);
    if (tray->handle) SDL_DestroyTray(tray->handle);
    bongo_cat_neo_image_free(&tray->icon);
    free(tray);
}
