#include "windows_diagnostics.h"

#ifdef _WIN32
#include <SDL3/SDL.h>
#include <dwmapi.h>
#include <stdlib.h>
#include <windows.h>

static void utf8(const wchar_t *source, char *target, size_t capacity) {
    if (!target || !capacity) return;
    target[0] = '\0';
    if (!source || !source[0]) return;
    int length = WideCharToMultiByte(CP_UTF8, 0, source, -1,
        target, (int)capacity, NULL, NULL);
    if (!length) target[0] = '\0';
    target[capacity - 1] = '\0';
}

static void log_displays(void) {
    for (DWORD index = 0; index < 16; ++index) {
        DISPLAY_DEVICEW adapter = {.cb = sizeof(adapter)};
        if (!EnumDisplayDevicesW(NULL, index, &adapter, 0)) break;
        char name[128], description[256], id[512];
        utf8(adapter.DeviceName, name, sizeof(name));
        utf8(adapter.DeviceString, description, sizeof(description));
        utf8(adapter.DeviceID, id, sizeof(id));
        SDL_Log("Display adapter %lu: name=%s description=%s id=%s flags=0x%lx",
            (unsigned long)index, name, description, id,
            (unsigned long)adapter.StateFlags);
        for (DWORD monitor_index = 0; monitor_index < 16; ++monitor_index) {
            DISPLAY_DEVICEW monitor = {.cb = sizeof(monitor)};
            if (!EnumDisplayDevicesW(adapter.DeviceName, monitor_index,
                &monitor, 0)) break;
            char monitor_name[128], monitor_description[256];
            utf8(monitor.DeviceName, monitor_name, sizeof(monitor_name));
            utf8(monitor.DeviceString, monitor_description,
                sizeof(monitor_description));
            SDL_Log("Display output %lu.%lu: name=%s description=%s "
                "flags=0x%lx", (unsigned long)index,
                (unsigned long)monitor_index, monitor_name,
                monitor_description, (unsigned long)monitor.StateFlags);
        }
    }
}

static void log_process(void) {
    HANDLE token = NULL;
    TOKEN_ELEVATION elevation = {0};
    DWORD length = 0, integrity = 0;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        GetTokenInformation(token, TokenElevation, &elevation,
            sizeof(elevation), &length);
        DWORD needed = 0;
        GetTokenInformation(token, TokenIntegrityLevel, NULL, 0, &needed);
        if (needed) {
            TOKEN_MANDATORY_LABEL *label = malloc(needed);
            if (label && GetTokenInformation(token, TokenIntegrityLevel,
                label, needed, &needed)) {
                PSID sid = label->Label.Sid;
                if (IsValidSid(sid) && *GetSidSubAuthorityCount(sid) > 0)
                    integrity = *GetSidSubAuthority(sid,
                        *GetSidSubAuthorityCount(sid) - 1);
            }
            free(label);
        }
        CloseHandle(token);
    }
    wchar_t executable_wide[MAX_PATH] = {0};
    char executable[MAX_PATH * 3];
    GetModuleFileNameW(NULL, executable_wide, MAX_PATH - 1);
    utf8(executable_wide, executable, sizeof(executable));
    SYSTEM_INFO system = {0};
    GetNativeSystemInfo(&system);
    SDL_Log("Process environment: pid=%lu native_arch=%u pointer_bits=%u "
        "elevated=%d integrity_rid=0x%lx executable=%s",
        (unsigned long)GetCurrentProcessId(),
        (unsigned)system.wProcessorArchitecture, (unsigned)(sizeof(void *) * 8),
        elevation.TokenIsElevated != 0, (unsigned long)integrity, executable);
}

static void log_window(HWND window) {
    char class_name[128], title[256];
    wchar_t class_wide[128] = {0}, title_wide[256] = {0};
    GetClassNameW(window, class_wide, (int)(sizeof(class_wide) /
        sizeof(class_wide[0])));
    GetWindowTextW(window, title_wide, (int)(sizeof(title_wide) /
        sizeof(title_wide[0])));
    utf8(class_wide, class_name, sizeof(class_name));
    utf8(title_wide, title, sizeof(title));
    RECT frame = {0};
    HRESULT frame_result = DwmGetWindowAttribute(window,
        DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame));
    LONG_PTR extended = GetWindowLongPtrW(window, GWL_EXSTYLE);
    BYTE alpha = 0; COLORREF color = 0; DWORD flags = 0;
    BOOL layered_known = (extended & WS_EX_LAYERED) != 0 &&
        GetLayeredWindowAttributes(window, &color, &alpha, &flags);
    typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    GetDpiForWindowFn get_dpi = user32 ? (GetDpiForWindowFn)(void *)
        GetProcAddress(user32, "GetDpiForWindow") : NULL;
    UINT dpi = get_dpi ? get_dpi(window) : 0;
    DWORD pid = 0; DWORD thread = GetWindowThreadProcessId(window, &pid);
    SDL_Log("Capture window details: class=%s title=%s pid=%lu thread=%lu "
        "dpi=%u frame_known=%d frame=%ld,%ld %ldx%ld "
        "layered_known=%d alpha=%u layered_flags=0x%lx", class_name, title,
        (unsigned long)pid, (unsigned long)thread, dpi,
        SUCCEEDED(frame_result), (long)frame.left, (long)frame.top,
        (long)(frame.right - frame.left), (long)(frame.bottom - frame.top),
        layered_known, (unsigned)alpha, (unsigned long)flags);
}

void bongo_cat_windows_diagnostics_log(HWND window) {
    OSVERSIONINFOW version = {.dwOSVersionInfoSize = sizeof(version)};
    typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOW *);
    HMODULE module = GetModuleHandleW(L"ntdll.dll");
    RtlGetVersionFn get_version = module ?
        (RtlGetVersionFn)(void *)GetProcAddress(module, "RtlGetVersion") : NULL;
    bool known = get_version && get_version(&version) == 0;
    BOOL composition = FALSE;
    HRESULT composition_result = DwmIsCompositionEnabled(&composition);
    SDL_Log("Windows diagnostics: version=%lu.%lu.%lu known=%d "
        "wgc_os_supported=%d remote_session=%d composition=%d "
        "composition_result=0x%08lx",
        (unsigned long)version.dwMajorVersion,
        (unsigned long)version.dwMinorVersion,
        (unsigned long)version.dwBuildNumber, known,
        known && version.dwBuildNumber >= 18362,
        GetSystemMetrics(SM_REMOTESESSION) != 0,
        composition, (unsigned long)composition_result);
    log_process();
    log_displays();
    if (window) log_window(window);
}
#endif
