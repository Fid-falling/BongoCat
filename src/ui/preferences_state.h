#ifndef BONGO_CAT_NEO_PREFERENCES_STATE_H
#define BONGO_CAT_NEO_PREFERENCES_STATE_H

#include "preferences_internal.h"
#include "ui_backend.h"
#include "bongo_cat_neo/i18n.h"
#include "bongo_cat_neo/preferences.h"

typedef struct BongoCatNeoPreferenceNotice {
    char message[384];
    uint64_t started_ns;
    uint64_t until_ns;
    bool error;
} BongoCatNeoPreferenceNotice;

struct BongoCatNeoPreferences {
    BongoCatNeoApp *app;
    SDL_Window *window;
    SDL_GLContext gl_context;
    bool owns_gl_context;
    BongoCatNeoUIBackend ui;
    unsigned int logo_texture;
    unsigned int icon_texture;
    unsigned int icon_texture_hidpi;
    int logo_width;
    int logo_height;
    unsigned int catime_texture;
    unsigned int vlaina_texture;
    int catime_width, catime_height;
    int vlaina_width, vlaina_height;
    bool support_assets_loaded;
    int page;
    int style_theme;
    BongoCatNeoLanguage font_language;
    nk_rune glyph_ranges[2048];
    bool input_active;
    bool import_requested;
    BongoCatNeoImportDialog *import_dialog;
    bool frame_checked;
    bool render_dirty;
    uint64_t last_render_ns;
    float scroll_current[5];
    float scroll_target[5];
    bool scroll_ready[5];
    bool page_seen;
    int last_page;
    uint64_t page_transition_ns;
    BongoCatNeoPreferenceNotice notices[4];
    bool behavior_dialog;
    int behavior_tab;
    uint64_t behavior_dialog_opened_ns;
    uint64_t behavior_dialog_closing_ns;
    uint64_t behavior_tab_transition_ns;
    float behavior_scroll[2];
    bool native_drag;
    bool chrome_dragging;
    bool live_resize_active;
    bool live_resize_rendering;
    int drag_window_x;
    int drag_window_y;
    float drag_pointer_x;
    float drag_pointer_y;
    char shortcut_id[BONGO_CAT_NEO_ID_CAP + 16];
    char *shortcut_target;
    int shortcut_capacity;
    char shortcut_original[BONGO_CAT_NEO_SHORTCUT_CAP];
    SDL_Keycode shortcut_key;
    bool shortcut_recording;
    uint64_t shortcut_suppress_until_ns;
};

int bongo_cat_neo_preferences_resolved_theme(const BongoCatNeoPreferences *value);
void bongo_cat_neo_preferences_apply_theme(BongoCatNeoPreferences *value);
void bongo_cat_neo_preferences_live_resize_install(BongoCatNeoPreferences *value);
void bongo_cat_neo_preferences_live_resize_uninstall(BongoCatNeoPreferences *value);
void bongo_cat_neo_preferences_assets_load(BongoCatNeoPreferences *value);
void bongo_cat_neo_preferences_support_assets_load(BongoCatNeoPreferences *value);
void bongo_cat_neo_preferences_assets_clear(BongoCatNeoPreferences *value);
void bongo_cat_neo_preferences_icon_draw(const BongoCatNeoPreferences *value,
    struct nk_command_buffer *canvas, int icon, struct nk_rect bounds,
    struct nk_color color);

#endif
