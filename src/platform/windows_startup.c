#include "bongo_cat_neo/platform.h"
#include "windows_startup.h"

#ifdef _WIN32
#include <SDL3/SDL.h>
#include <ctype.h>
#include <string.h>
#include <windows.h>

static HANDLE instance_mutex;
static wchar_t instance_title[96] = BONGO_CAT_NEO_NAME_W;
static wchar_t instance_mutex_name[128] = L"Local\\BongoCatNeo.SingleInstance";
static bool identity_ready;

static bool safe_identity(const char *value) {
    if (!value || !value[0] || strlen(value) > 32) return false;
    for (const unsigned char *cursor = (const unsigned char *)value; *cursor; ++cursor)
        if (!isalnum(*cursor) && *cursor != '-' && *cursor != '_') return false;
    return true;
}

static void initialize_identity(void) {
    if (identity_ready) return;
    identity_ready = true;
    const char *value = SDL_getenv_unsafe("BONGO_CAT_NEO_TEST_INSTANCE_ID");
    if (!safe_identity(value)) return;
    swprintf(instance_title, sizeof(instance_title) / sizeof(instance_title[0]),
        L"%ls [%hs]", BONGO_CAT_NEO_NAME_W, value);
    swprintf(instance_mutex_name,
        sizeof(instance_mutex_name) / sizeof(instance_mutex_name[0]),
        L"Local\\BongoCatNeo.SingleInstance.%hs", value);
}

const wchar_t *bongo_cat_neo_windows_instance_title(void) {
    initialize_identity(); return instance_title;
}

bool bongo_cat_neo_platform_single_instance_begin(void) {
    initialize_identity();
    if (SDL_getenv_unsafe("BONGO_CAT_NEO_ALLOW_TEST_INSTANCES")) return true;
    instance_mutex = CreateMutexW(NULL, FALSE, instance_mutex_name);
    if (!instance_mutex) return true;
    if (GetLastError() != ERROR_ALREADY_EXISTS) return true;
    HWND existing = FindWindowW(NULL, instance_title);
    for (int attempt = 0; !existing && attempt < 30; ++attempt) {
        Sleep(100); existing = FindWindowW(NULL, instance_title);
    }
    if (existing) {
        ShowWindowAsync(existing, IsIconic(existing) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(existing);
        CloseHandle(instance_mutex); instance_mutex = NULL;
        return false;
    }
    CloseHandle(instance_mutex); instance_mutex = NULL;
    instance_mutex = CreateMutexW(NULL, FALSE, instance_mutex_name);
    if (instance_mutex && GetLastError() != ERROR_ALREADY_EXISTS) return true;
    if (instance_mutex) CloseHandle(instance_mutex);
    instance_mutex = NULL;
    return false;
}

void bongo_cat_neo_platform_single_instance_end(void) {
    if (instance_mutex) CloseHandle(instance_mutex);
    instance_mutex = NULL;
}

BongoCatNeoResult bongo_cat_neo_platform_set_autostart(bool enabled,
    BongoCatNeoError *error) {
    HKEY key;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, NULL, 0,
        KEY_SET_VALUE, NULL, &key, NULL);
    if (result != ERROR_SUCCESS) {
        bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_PLATFORM,
            "Cannot open autostart registry key");
        return BONGO_CAT_NEO_ERROR_PLATFORM;
    }
    if (enabled) {
        wchar_t executable[BONGO_CAT_NEO_PATH_CAP];
        DWORD length = GetModuleFileNameW(NULL, executable, BONGO_CAT_NEO_PATH_CAP);
        if (!length || length >= BONGO_CAT_NEO_PATH_CAP) {
            RegCloseKey(key); bongo_cat_neo_error_set(error,
                BONGO_CAT_NEO_ERROR_PLATFORM, "Cannot determine executable path");
            return BONGO_CAT_NEO_ERROR_PLATFORM;
        }
        wchar_t command[BONGO_CAT_NEO_PATH_CAP + 20];
        swprintf(command, BONGO_CAT_NEO_PATH_CAP + 20, L"\"%ls\" --autostart", executable);
        result = RegSetValueExW(key, BONGO_CAT_NEO_NAME_W, 0, REG_SZ,
            (const BYTE *)command, (DWORD)((wcslen(command) + 1) * sizeof(wchar_t)));
    } else result = RegDeleteValueW(key, BONGO_CAT_NEO_NAME_W);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
        bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_PLATFORM,
            "Cannot update autostart setting");
        return BONGO_CAT_NEO_ERROR_PLATFORM;
    }
    return BONGO_CAT_NEO_OK;
}
#endif
