#include "bongo_cat/platform.h"
#include "windows_borderless.h"
#include "windows_direct_input.h"
#include "windows_keys.h"
#include "windows_layered.h"
#include "windows_startup.h"
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
    BongoCatPlatform *platform;
    SRWLOCK platform_lock;
    HANDLE thread;
    HANDLE ready;
    DWORD thread_id;
    HHOOK keyboard;
    HHOOK mouse;
    BongoCatWindowsKeyboard keyboard_state;
    UINT test_drop_key_up;
    bool hooks_ready;
} WindowsState;
static WindowsState *global_state;
static void wake_main_thread(void) {
    WindowsState *state = global_state;
    if (!state || !state->platform) return;
    SDL_Event wake = {0};
    wake.type = state->platform->wake_event_type;
    SDL_PushEvent(&wake);
}
static HWND native_window(BongoCatPlatform *platform) {
    return (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(platform->window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
}
static void push_event(BongoCatInputKind kind, const char *name, float value) {
    WindowsState *state = global_state;
    if (!state || !name) return;
    AcquireSRWLockShared(&state->platform_lock);
    BongoCatPlatform *platform = state->platform;
    if (!platform) { ReleaseSRWLockShared(&state->platform_lock); return; }
    BongoCatInputEvent event = {0};
    event.kind = kind;
    event.timestamp_ms = GetTickCount64();
    event.value = value;
    snprintf(event.name, sizeof(event.name), "%s", name);
    if (bongo_cat_input_push(platform->input, &event))
        wake_main_thread();
    ReleaseSRWLockShared(&state->platform_lock);
}
static void emit_key(bool down, const char *name, void *userdata) {
    (void)userdata;
    push_event(down ? BONGO_CAT_INPUT_KEY_DOWN : BONGO_CAT_INPUT_KEY_UP,
        name, down ? 1.0f : 0.0f);
}
static LRESULT CALLBACK keyboard_hook(int code, WPARAM message, LPARAM data) {
    WindowsState *state = global_state;
    if (code == HC_ACTION && state) bongo_cat_windows_keyboard_event(
        &state->keyboard_state, (const KBDLLHOOKSTRUCT *)data, message,
        &state->test_drop_key_up, emit_key, NULL);
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
            BongoCatPlatform *platform = state->platform;
            if (platform && bongo_cat_input_mouse(platform->input,
                mouse->pt.x, mouse->pt.y)) wake_main_thread();
            ReleaseSRWLockShared(&state->platform_lock);
        } else {
            const char *name = mouse_button(message, mouse->mouseData);
            bool down = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
                message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN;
            if (name) push_event(down ? BONGO_CAT_INPUT_MOUSE_DOWN : BONGO_CAT_INPUT_MOUSE_UP,
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
    UINT_PTR reconcile_timer = SetTimer(NULL, 0, 25, NULL);
    state->hooks_ready = state->keyboard && state->mouse;
    SetEvent(state->ready);
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        if (reconcile_timer && message.message == WM_TIMER &&
            message.wParam == reconcile_timer)
            bongo_cat_windows_keyboard_reconcile(&state->keyboard_state,
                GetTickCount64(), emit_key, NULL);
        else {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (reconcile_timer) KillTimer(NULL, reconcile_timer);
    if (state->keyboard) UnhookWindowsHookEx(state->keyboard);
    if (state->mouse) UnhookWindowsHookEx(state->mouse);
    global_state = NULL;
    return 0;
}

BongoCatResult bongo_cat_platform_init(BongoCatPlatform *platform, SDL_Window *window,
    BongoCatInputState *input, BongoCatError *error) {
    if (!platform || !window || !input) return BONGO_CAT_ERROR_ARGUMENT;
    memset(platform, 0, sizeof(*platform));
    platform->window = window; platform->input = input;
    platform->window_opacity = 1.0f; platform->presenter = bongo_cat_windows_layered_create();
    if (!platform->presenter) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Cannot allocate the Windows layered presenter");
        return BONGO_CAT_ERROR_MEMORY;
    }
    platform->wake_event_type = SDL_RegisterEvents(1);
    if (platform->wake_event_type == (Uint32)-1) {
        bongo_cat_windows_layered_destroy(platform);
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Cannot reserve the Windows input wake event");
        return BONGO_CAT_ERROR_PLATFORM;
    }
    WindowsState *state = calloc(1, sizeof(*state));
    if (!state) {
        bongo_cat_windows_layered_destroy(platform);
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Cannot allocate Windows input state"); return BONGO_CAT_ERROR_MEMORY;
    }
    InitializeSRWLock(&state->platform_lock);
    state->platform = platform;
    const char *drop_key_up = SDL_getenv("BONGO_CAT_TEST_DROP_KEY_UP");
    if (drop_key_up) state->test_drop_key_up =
        (UINT)strtoul(drop_key_up, NULL, 10);
    bool test_failure = SDL_getenv("BONGO_CAT_TEST_HOOK_FAILURE") != NULL;
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
    if (!bongo_cat_windows_direct_input_create(platform, hwnd))
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Mver-compatible DirectInput mouse tracking is unavailable");
    SetWindowTextW(hwnd, bongo_cat_windows_instance_title());
    bongo_cat_windows_borderless_install(hwnd);
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
    return BONGO_CAT_OK;
}
void bongo_cat_platform_shutdown(BongoCatPlatform *platform) {
    WindowsState *state = platform ? platform->native : NULL;
    if (!platform) return;
    if (!state) { bongo_cat_windows_layered_destroy(platform); return; }
    HWND window = native_window(platform);
    if (window) bongo_cat_windows_borderless_uninstall(window);
    AcquireSRWLockExclusive(&state->platform_lock);
    state->platform = NULL;
    ReleaseSRWLockExclusive(&state->platform_lock);
    bongo_cat_windows_direct_input_destroy(platform);
    if (state->thread_id) PostThreadMessageW(state->thread_id, WM_QUIT, 0, 0);
    if (state->thread && WaitForSingleObject(state->thread, 3000) != WAIT_OBJECT_0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Windows input hook thread did not stop; its state will remain isolated until exit");
        bongo_cat_windows_layered_destroy(platform);
        platform->native = NULL; return;
    }
    if (state->thread) CloseHandle(state->thread);
    if (state->ready) CloseHandle(state->ready);
    free(state);
    bongo_cat_windows_layered_destroy(platform);
    platform->native = NULL;
}

static HWND desktop_anchor(void) {
    HWND program_manager = FindWindowW(L"Progman", NULL);
    if (!program_manager) return NULL;
    HWND worker = NULL;
    while ((worker = FindWindowExW(NULL, worker, L"WorkerW", NULL)) != NULL)
        if (FindWindowExW(worker, NULL, L"SHELLDLL_DefView", NULL))
            return worker;
    return program_manager;
}

static void apply_window_level(HWND window, bool topmost) {
    if (!window) return;
    RECT before = {0};
    bool positioned = GetWindowRect(window, &before) != FALSE;
    HWND owner = topmost ? NULL : desktop_anchor();
    SetWindowLongPtrW(window, GWLP_HWNDPARENT, (LONG_PTR)owner);
    SetWindowPos(window, topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
        0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
        SWP_FRAMECHANGED);
    if (!topmost)
        SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (positioned) {
        RECT after = {0};
        if (GetWindowRect(window, &after) &&
            (after.left != before.left || after.top != before.top))
            SetWindowPos(window, NULL, before.left, before.top, 0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void bongo_cat_platform_set_always_on_top(BongoCatPlatform *platform, bool enabled) {
    if (!platform || !platform->window) return;
    bool applied = SDL_SetWindowAlwaysOnTop(platform->window, enabled);
    HWND window = native_window(platform);
    if (!applied && window)
        SetWindowPos(window, enabled ? HWND_TOPMOST : HWND_NOTOPMOST,
            0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    apply_window_level(window, enabled);
    bongo_cat_windows_layered_set_always_on_top(platform, enabled);
}
void bongo_cat_platform_begin_drag(BongoCatPlatform *platform,
    BongoCatModalTick modal_tick, void *userdata) {
    HWND hwnd = native_window(platform);
    bongo_cat_windows_begin_drag(hwnd, modal_tick, userdata);
}
bool bongo_cat_platform_dynamic_hit_supported(void) { return true; }
void bongo_cat_platform_relative_pointer_reset(BongoCatPlatform *platform) {
    bongo_cat_windows_direct_input_reset(platform);
}
#endif
