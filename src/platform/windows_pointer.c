#include "bongo_cat/platform.h"
#include "windows_borderless.h"

#ifdef _WIN32
#include <SDL3/SDL.h>
#include <SDL3/SDL_properties.h>
#include <dwmapi.h>
#include <windows.h>

static HWND native_window(BongoCatPlatform *platform) {
    return (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(platform->window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
}

bool bongo_cat_platform_pointer_local(BongoCatPlatform *platform, double screen_x,
    double screen_y, float *local_x, float *local_y) {
    if (!platform || !local_x || !local_y) return false;
    HWND window = native_window(platform);
    POINT point = {(LONG)screen_x, (LONG)screen_y};
    RECT client;
    if (!window || !ScreenToClient(window, &point) || !GetClientRect(window, &client))
        return false;
    *local_x = (float)point.x; *local_y = (float)point.y;
    return point.x >= client.left && point.x < client.right &&
        point.y >= client.top && point.y < client.bottom;
}

static void update_style(BongoCatPlatform *platform, LONG_PTR add,
    LONG_PTR remove, bool refresh_frame) {
    HWND window = native_window(platform);
    if (!window) return;
    LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    LONG_PTR next = (style | add) & ~remove;
    if (next == style) return;
    SetWindowLongPtrW(window, GWL_EXSTYLE, next);
    UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE;
    if (refresh_frame) flags |= SWP_FRAMECHANGED;
    SetWindowPos(window, NULL, 0, 0, 0, 0, flags);
}

static void refresh_transparency(HWND window) {
    MARGINS margins = {-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(window, &margins);
    HRGN region = CreateRectRgn(-1, -1, 0, 0);
    if (!region) return;
    DWM_BLURBEHIND blur = {0};
    blur.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
    blur.fEnable = TRUE;
    blur.hRgnBlur = region;
    DwmEnableBlurBehindWindow(window, &blur);
    DeleteObject(region);
}

void bongo_cat_platform_set_click_through(BongoCatPlatform *platform, bool enabled) {
    HWND window = native_window(platform);
    bongo_cat_windows_borderless_set_click_through(window, enabled);
    update_style(platform, enabled ? WS_EX_TRANSPARENT : 0,
        enabled ? 0 : WS_EX_TRANSPARENT, true);
}

void bongo_cat_platform_set_taskbar(BongoCatPlatform *platform, bool visible) {
    HWND window = native_window(platform);
    if (!window) return;
    LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    LONG_PTR next = (style | (visible ? WS_EX_APPWINDOW : WS_EX_TOOLWINDOW)) &
        ~(visible ? WS_EX_TOOLWINDOW : WS_EX_APPWINDOW);
    if (next == style) return;
    bool shown = IsWindowVisible(window) != FALSE;
    if (shown) ShowWindow(window, SW_HIDE);
    SetWindowLongPtrW(window, GWL_EXSTYLE, next);
    SetWindowPos(window, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE |
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    refresh_transparency(window);
    if (shown) {
        ShowWindow(window, SW_SHOWNOACTIVATE);
        UpdateWindow(window);
    }
}

static const wchar_t tray_proc_property[] = L"BongoCat.TrayWindowProc";
static BongoCatTrayClick tray_click;
static void *tray_click_userdata;

static LRESULT CALLBACK tray_window_proc(HWND window, UINT message,
    WPARAM wparam, LPARAM lparam) {
    WNDPROC original = (WNDPROC)GetPropW(window, tray_proc_property);
    if (message == WM_USER + 1 && LOWORD(lparam) == WM_LBUTTONUP && tray_click) {
        tray_click(tray_click_userdata);
        return 0;
    }
    return CallWindowProcW(original ? original : DefWindowProcW,
        window, message, wparam, lparam);
}

static void bind_tray_window(HWND window, void *tray, bool binding) {
    wchar_t name[32]; DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId() ||
        !GetClassNameW(window, name, (int)(sizeof(name) / sizeof(name[0]))) ||
        wcscmp(name, L"Message") != 0 ||
        (void *)GetWindowLongPtrW(window, GWLP_USERDATA) != tray) return;
    WNDPROC original = (WNDPROC)GetWindowLongPtrW(window, GWLP_WNDPROC);
    if (binding) {
        if (!GetPropW(window, tray_proc_property) && original != tray_window_proc &&
            SetPropW(window, tray_proc_property, (HANDLE)original)) {
            SetLastError(ERROR_SUCCESS);
            if (!SetWindowLongPtrW(window, GWLP_WNDPROC,
                (LONG_PTR)tray_window_proc) && GetLastError() != ERROR_SUCCESS)
                RemovePropW(window, tray_proc_property);
        }
    } else if (GetPropW(window, tray_proc_property)) {
        if ((WNDPROC)GetWindowLongPtrW(window, GWLP_WNDPROC) == tray_window_proc)
            SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)GetPropW(
                window, tray_proc_property));
        RemovePropW(window, tray_proc_property);
    }
}

void bongo_cat_platform_set_tray_left_click(void *tray, BongoCatTrayClick callback,
    void *userdata) {
    tray_click = callback;
    tray_click_userdata = userdata;
    /* Pinned SDL stores SDL_Tray* in its private message window userdata. */
    HWND window = NULL;
    while ((window = FindWindowExW(HWND_MESSAGE, window, L"Message", NULL)) != NULL)
        bind_tray_window(window, tray, callback != NULL);
}

void bongo_cat_platform_raise_window(SDL_Window *window) {
    if (!window) return;
    SDL_ShowWindow(window);
    HWND handle = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    if (!handle) { SDL_RaiseWindow(window); return; }
    if (IsIconic(handle)) ShowWindow(handle, SW_RESTORE);
    HWND foreground = GetForegroundWindow();
    DWORD foreground_thread = foreground ? GetWindowThreadProcessId(foreground, NULL) : 0;
    DWORD current_thread = GetCurrentThreadId();
    bool attached = foreground_thread && foreground_thread != current_thread &&
        AttachThreadInput(current_thread, foreground_thread, TRUE);
    BringWindowToTop(handle); SetForegroundWindow(handle); SetActiveWindow(handle);
    if (attached) AttachThreadInput(current_thread, foreground_thread, FALSE);
}

bool bongo_cat_platform_set_geometry(BongoCatPlatform *platform,
    int x, int y, int width, int height) {
    if (!platform || !platform->window) return false;
    int current_width, current_height;
    bool size_known = SDL_GetWindowSize(platform->window,
        &current_width, &current_height);
    int current_x, current_y;
    bool position_known = SDL_GetWindowPosition(platform->window,
        &current_x, &current_y);
    bool size_changed = !size_known || current_width != width || current_height != height;
    bool position_changed = !position_known || current_x != x || current_y != y;
    if (!size_changed && !position_changed) return true;
    HWND window = native_window(platform);
    if (!window) return false;
    if (size_changed &&
        !(SDL_GetWindowFlags(platform->window) & SDL_WINDOW_RESIZABLE) &&
        !SDL_SetWindowResizable(platform->window, true))
        return false;
    bool changed = SetWindowPos(window, NULL, position_changed ? x : current_x,
        position_changed ? y : current_y, size_changed ? width : current_width,
        size_changed ? height : current_height,
        SWP_NOZORDER | SWP_NOACTIVATE) != 0;
    if (!changed) return false;
    return SDL_SyncWindow(platform->window);
}
#endif
