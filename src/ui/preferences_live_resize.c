#include "preferences_state.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <SDL3/SDL_properties.h>

static const wchar_t original_property[] = L"BongoCat.PreferenceResizeProc";
static const wchar_t value_property[] = L"BongoCat.PreferenceResizeValue";

static HWND native_window(BongoCatPreferences *value) {
    return value && value->window ? (HWND)SDL_GetPointerProperty(
        SDL_GetWindowProperties(value->window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL) : NULL;
}

static void render_live(BongoCatPreferences *value) {
    if (!value || !value->window || value->live_resize_rendering) return;
    value->live_resize_rendering = true;
    if (!value->input_active) bongo_cat_preferences_input_begin(value);
    value->render_dirty = true;
    bongo_cat_preferences_render(value);
    value->live_resize_rendering = false;
}

static bool capture_live(BongoCatPreferences *value) {
    if (!value || !SDL_GL_MakeCurrent(value->window, value->gl_context))
        return false;
    bool result = bongo_cat_ui_resize_cache_capture(&value->ui);
    SDL_GL_MakeCurrent(value->app->window, value->app->gl_context);
    return result;
}

static bool present_live(BongoCatPreferences *value) {
    if (!value || !SDL_GL_MakeCurrent(value->window, value->gl_context))
        return false;
    bool result = bongo_cat_ui_resize_cache_present(&value->ui) &&
        SDL_GL_SwapWindow(value->window);
    SDL_GL_MakeCurrent(value->app->window, value->app->gl_context);
    if (result) bongo_cat_preferences_record_frame(value);
    return result;
}

static LRESULT CALLBACK live_resize_proc(HWND window, UINT message,
    WPARAM wparam, LPARAM lparam) {
    WNDPROC original = (WNDPROC)GetPropW(window, original_property);
    BongoCatPreferences *value = (BongoCatPreferences *)GetPropW(
        window, value_property);
    bool enter = value && message == WM_ENTERSIZEMOVE;
    bool resize = value && value->live_resize_active && message == WM_SIZE;
    LRESULT result = CallWindowProcW(original ? original : DefWindowProcW,
        window, message, wparam, lparam);
    if (enter) {
        value->live_resize_active = true;
        capture_live(value);
    }
    if (resize && !present_live(value)) render_live(value);
    if (value && message == WM_EXITSIZEMOVE) {
        value->live_resize_active = false;
        render_live(value);
        if (SDL_GL_MakeCurrent(value->window, value->gl_context)) {
            bongo_cat_ui_resize_cache_destroy(&value->ui);
            SDL_GL_MakeCurrent(value->app->window, value->app->gl_context);
        }
    }
    return result;
}

void bongo_cat_preferences_live_resize_install(BongoCatPreferences *value) {
    HWND window = native_window(value);
    if (!window || GetPropW(window, original_property)) return;
    WNDPROC original = (WNDPROC)GetWindowLongPtrW(window, GWLP_WNDPROC);
    if (!original || !SetPropW(window, original_property, (HANDLE)original)) return;
    if (!SetPropW(window, value_property, (HANDLE)value)) {
        RemovePropW(window, original_property);
        return;
    }
    if (!SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)live_resize_proc)) {
        RemovePropW(window, value_property);
        RemovePropW(window, original_property);
    }
}

void bongo_cat_preferences_live_resize_uninstall(BongoCatPreferences *value) {
    if (!value) return;
    HWND window = native_window(value);
    WNDPROC original = window ? (WNDPROC)GetPropW(window, original_property) : NULL;
    if (original) SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)original);
    if (window) {
        RemovePropW(window, value_property);
        RemovePropW(window, original_property);
    }
    value->live_resize_active = false;
    value->live_resize_rendering = false;
}
#else
void bongo_cat_preferences_live_resize_install(BongoCatPreferences *value) {
    (void)value;
}
void bongo_cat_preferences_live_resize_uninstall(BongoCatPreferences *value) {
    (void)value;
}
#endif
