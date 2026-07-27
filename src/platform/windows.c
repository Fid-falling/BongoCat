#include "bongo_cat_neo/platform.h"
#include "windows_borderless.h"
#include "windows_keys.h"
#include "windows_startup.h"
#include "../ui/ui_native_theme.h"
#ifdef _WIN32
#include <SDL3/SDL.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
typedef struct WindowsState {
    BongoCatNeoPlatform *platform;
    SRWLOCK platform_lock;
    HANDLE thread;
    HANDLE ready;
    DWORD thread_id;
    HHOOK keyboard;
    HHOOK mouse;
    bool key_down[BONGO_CAT_NEO_INPUT_KEY_STATE_CAP];
    bool hooks_ready;
} WindowsState;

static WindowsState *global_state;
static void wake_main_thread(void) {
    SDL_Event wake = {.type = SDL_EVENT_USER};
    SDL_PushEvent(&wake);
}
static HWND native_window(BongoCatNeoPlatform *platform) {
    return (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(platform->window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
}
static void push_event(BongoCatNeoInputKind kind, const char *name, float value) {
    WindowsState *state = global_state;
    if (!state || !name) return;
    AcquireSRWLockShared(&state->platform_lock);
    BongoCatNeoPlatform *platform = state->platform;
    if (!platform) { ReleaseSRWLockShared(&state->platform_lock); return; }
    BongoCatNeoInputEvent event = {0};
    event.kind = kind;
    event.timestamp_ms = GetTickCount64();
    event.value = value;
    snprintf(event.name, sizeof(event.name), "%s", name);
    if (bongo_cat_neo_input_push(platform->input, &event))
        wake_main_thread();
    ReleaseSRWLockShared(&state->platform_lock);
}
static LRESULT CALLBACK keyboard_hook(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION) {
        WindowsState *state = global_state;
        const KBDLLHOOKSTRUCT *key = (const KBDLLHOOKSTRUCT *)data;
        bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
        char buffer[16];
        const char *name = bongo_cat_neo_windows_key_name(key, buffer);
        if (state && name && (down || up) && bongo_cat_neo_input_edge(
            state->key_down, key->vkCode, down))
            push_event(down ? BONGO_CAT_NEO_INPUT_KEY_DOWN :
                BONGO_CAT_NEO_INPUT_KEY_UP, name, down ? 1.0f : 0.0f);
    }
    return CallNextHookEx(NULL, code, message, data);
}

static const char *mouse_button(WPARAM message, DWORD mouse_data) {
    switch (message) {
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: return "Left";
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: return "Right";
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: return "Middle";
    case WM_XBUTTONDOWN: case WM_XBUTTONUP:
        return HIWORD(mouse_data) == XBUTTON1 ? "Back" : "Forward";
    default: return NULL;
    }
}

static LRESULT CALLBACK mouse_hook(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION && global_state) {
        const MSLLHOOKSTRUCT *mouse = (const MSLLHOOKSTRUCT *)data;
        if (message == WM_MOUSEMOVE) {
            WindowsState *state = global_state;
            AcquireSRWLockShared(&state->platform_lock);
            BongoCatNeoPlatform *platform = state->platform;
            if (platform && bongo_cat_neo_input_mouse(platform->input,
                mouse->pt.x, mouse->pt.y)) wake_main_thread();
            ReleaseSRWLockShared(&state->platform_lock);
        } else {
            const char *name = mouse_button(message, mouse->mouseData);
            bool down = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
                message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN;
            if (name) push_event(down ? BONGO_CAT_NEO_INPUT_MOUSE_DOWN : BONGO_CAT_NEO_INPUT_MOUSE_UP,
                name, down ? 1.0f : 0.0f);
        }
    }
    return CallNextHookEx(NULL, code, message, data);
}

static DWORD WINAPI hook_thread(void *context) {
    WindowsState *state = context;
    global_state = state;
    MSG message;
    PeekMessageW(&message, NULL, WM_USER, WM_USER, PM_NOREMOVE);
    state->keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_hook, NULL, 0);
    state->mouse = SetWindowsHookExW(WH_MOUSE_LL, mouse_hook, NULL, 0);
    state->hooks_ready = state->keyboard && state->mouse;
    SetEvent(state->ready);
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (state->keyboard) UnhookWindowsHookEx(state->keyboard);
    if (state->mouse) UnhookWindowsHookEx(state->mouse);
    global_state = NULL;
    return 0;
}

BongoCatNeoResult bongo_cat_neo_platform_init(BongoCatNeoPlatform *platform, SDL_Window *window,
    BongoCatNeoInputState *input, BongoCatNeoError *error) {
    if (!platform || !window || !input) return BONGO_CAT_NEO_ERROR_ARGUMENT;
    memset(platform, 0, sizeof(*platform));
    platform->window = window;
    platform->input = input;
    WindowsState *state = calloc(1, sizeof(*state));
    if (!state) {
        bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_MEMORY,
            "Cannot allocate Windows input state"); return BONGO_CAT_NEO_ERROR_MEMORY;
    }
    InitializeSRWLock(&state->platform_lock);
    state->platform = platform;
    bool test_failure = SDL_getenv("BONGO_CAT_NEO_TEST_HOOK_FAILURE") != NULL;
    state->ready = test_failure ? NULL : CreateEventW(NULL, TRUE, FALSE, NULL);
    state->thread = state->ready
        ? CreateThread(NULL, 0, hook_thread, state, 0, &state->thread_id) : NULL;
    platform->native = state;
    if (!state->ready || !state->thread || WaitForSingleObject(state->ready, 3000) != WAIT_OBJECT_0 ||
        !state->hooks_ready) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Global input hooks are unavailable; the window will continue without global input");
    }
    HWND hwnd = native_window(platform);
    SetWindowTextW(hwnd, bongo_cat_neo_windows_instance_title());
    bongo_cat_neo_windows_borderless_install(hwnd);
    if (!SDL_SetWindowResizable(window, true)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Borderless resize is unavailable: %s", SDL_GetError());
    }
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_TRANSPARENT) {
        MARGINS margins = {-1, -1, -1, -1};
        DwmExtendFrameIntoClientArea(hwnd, &margins);
    }
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE |
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return BONGO_CAT_NEO_OK;
}
void bongo_cat_neo_platform_shutdown(BongoCatNeoPlatform *platform) {
    WindowsState *state = platform ? platform->native : NULL;
    if (!state) return;
    HWND window = native_window(platform);
    if (window) bongo_cat_neo_windows_borderless_uninstall(window);
    AcquireSRWLockExclusive(&state->platform_lock);
    state->platform = NULL;
    ReleaseSRWLockExclusive(&state->platform_lock);
    if (state->thread_id) PostThreadMessageW(state->thread_id, WM_QUIT, 0, 0);
    if (state->thread && WaitForSingleObject(state->thread, 3000) != WAIT_OBJECT_0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Windows input hook thread did not stop; its state will remain isolated until exit");
        platform->native = NULL; return;
    }
    if (state->thread) CloseHandle(state->thread);
    if (state->ready) CloseHandle(state->ready);
    free(state);
    platform->native = NULL;
}
void bongo_cat_neo_platform_set_always_on_top(BongoCatNeoPlatform *platform, bool enabled) {
    if (!platform || !platform->window) return;
    if (SDL_SetWindowAlwaysOnTop(platform->window, enabled)) return;
    HWND window = native_window(platform);
    if (window) SetWindowPos(window, enabled ? HWND_TOPMOST : HWND_NOTOPMOST,
        0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}
void bongo_cat_neo_platform_begin_drag(BongoCatNeoPlatform *platform) {
    HWND hwnd = native_window(platform);
    ReleaseCapture();
    SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
}

bool bongo_cat_neo_platform_dynamic_hit_supported(void) { return true; }

static wchar_t *wide(const char *text) {
    if (!text) return NULL;
    int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    wchar_t *value = length > 0 ? calloc((size_t)length, sizeof(*value)) : NULL;
    if (value) MultiByteToWideChar(CP_UTF8, 0, text, -1, value, length);
    return value;
}

static void menu_text(HMENU menu, UINT flags, UINT_PTR id, const char *text) {
    wchar_t *label = wide(text);
    AppendMenuW(menu, flags, id, label ? label : L"");
    free(label);
}

BongoCatNeoMenuAction bongo_cat_neo_platform_context_menu(BongoCatNeoPlatform *platform,
    const BongoCatNeoMenuLabels *labels) {
    if (!platform || !labels) return BONGO_CAT_NEO_MENU_NONE;
    HMENU menu = CreatePopupMenu(), sizes = CreatePopupMenu(), opacity = CreatePopupMenu();
    HMENU models = CreatePopupMenu();
    HMENU motions = labels->motion_count ? CreatePopupMenu() : NULL;
    HMENU expressions = labels->expression_count ? CreatePopupMenu() : NULL;
    if (!menu || !sizes || !opacity || !models ||
        (labels->motion_count && !motions) || (labels->expression_count && !expressions))
        return BONGO_CAT_NEO_MENU_NONE;
    menu_text(menu, MF_STRING, BONGO_CAT_NEO_MENU_PREFERENCES, labels->preferences);
    menu_text(menu, MF_STRING, BONGO_CAT_NEO_MENU_HIDE, labels->hide);
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    menu_text(menu, MF_STRING | (labels->pass_through_checked ? MF_CHECKED : 0),
        BONGO_CAT_NEO_MENU_PASS_THROUGH, labels->pass_through);
    menu_text(menu, MF_STRING | (labels->always_on_top_checked ? MF_CHECKED : 0),
        BONGO_CAT_NEO_MENU_ALWAYS_ON_TOP, labels->always_on_top);
    for (int i = 0; i < 16; ++i) {
        int scale = 50 + i * 10;
        wchar_t label[16]; swprintf(label, 16, L"%d%%", scale);
        UINT flags = MF_STRING | (SDL_fabsf(labels->scale_percent - scale) < .5f ? MF_CHECKED : 0);
        AppendMenuW(sizes, flags, BONGO_CAT_NEO_MENU_SCALE_50 + i, label);
    }
    menu_text(sizes, MF_STRING | MF_DISABLED | MF_GRAYED, 0,
        labels->wheel_size_hint);
    const int opacities[] = {10,20,30,40,50,60,70,80,90,100};
    for (int i = 0; i < 10; ++i) {
        wchar_t label[16]; swprintf(label, 16, L"%d%%", opacities[i]);
        UINT flags = MF_STRING | (SDL_fabsf(labels->opacity_percent - opacities[i]) < .5f ? MF_CHECKED : 0);
        AppendMenuW(opacity, flags, BONGO_CAT_NEO_MENU_OPACITY_10 + i, label);
    }
    menu_text(opacity, MF_STRING | MF_DISABLED | MF_GRAYED, 0,
        labels->wheel_opacity_hint);
    for (size_t i = 0; i < labels->motion_count; ++i)
        menu_text(motions, MF_STRING, BONGO_CAT_NEO_MENU_MOTION_FIRST + i,
            labels->motion_names[i]);
    for (size_t i = 0; i < labels->expression_count; ++i)
        menu_text(expressions, MF_STRING, BONGO_CAT_NEO_MENU_EXPRESSION_FIRST + i,
            labels->expression_names[i]);
    for (size_t i = 0; i < labels->model_count; ++i)
        menu_text(models, MF_STRING | (i == labels->current_model ? MF_CHECKED : 0),
            BONGO_CAT_NEO_MENU_MODEL_FIRST + i, labels->model_names[i]);
    wchar_t *size_label = wide(labels->window_size), *opacity_label = wide(labels->opacity);
    wchar_t *model_label = wide(labels->model), *motion_label = wide(labels->motion);
    wchar_t *expression_label = wide(labels->expression);
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)sizes, size_label ? size_label : L"");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)opacity, opacity_label ? opacity_label : L"");
    if (labels->motion_count) AppendMenuW(menu, MF_POPUP, (UINT_PTR)motions,
        motion_label ? motion_label : L"");
    if (labels->expression_count) AppendMenuW(menu, MF_POPUP, (UINT_PTR)expressions,
        expression_label ? expression_label : L"");
    AppendMenuW(menu, MF_POPUP | (labels->model_count ? 0 : MF_GRAYED),
        (UINT_PTR)models, model_label ? model_label : L"");
    free(size_label); free(opacity_label); free(model_label); free(motion_label);
    free(expression_label);
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    menu_text(menu, MF_STRING, BONGO_CAT_NEO_MENU_EXIT, labels->exit);
    POINT point; GetCursorPos(&point);
    HWND window = native_window(platform);
    SetForegroundWindow(window);
    bongo_cat_neo_ui_native_menu_prepare(platform->window, labels->dark_theme);
    bongo_cat_neo_windows_menu_preview(window, labels->preview,
        labels->preview_tick, labels->preview_userdata);
    UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
        point.x, point.y, 0, window, NULL);
    bongo_cat_neo_windows_menu_preview(NULL, NULL, NULL, NULL);
    if (labels->restore) labels->restore(labels->preview_userdata, (BongoCatNeoMenuAction)command);
    DestroyMenu(menu);
    return (BongoCatNeoMenuAction)command;
}

#endif
