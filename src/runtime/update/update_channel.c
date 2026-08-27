#include "update_internal.h"

#ifdef _WIN32
#include "windows_package.h"
#include <windows.h>

bool bongo_cat_update_platform_supported(void) {
    return true;
}

bool bongo_cat_update_platform_store(void) {
    return bongo_cat_windows_is_packaged();
}

static bool current_executable(wchar_t *path, size_t capacity) {
    DWORD length = GetModuleFileNameW(NULL, path, (DWORD)capacity);
    return length > 0 && length < capacity;
}

bool bongo_cat_update_platform_installed(void) {
    wchar_t running[2048] = {0};
    if (!current_executable(running, _countof(running))) return false;
    const DWORD views[] = {
        RRF_SUBKEY_WOW6464KEY, RRF_SUBKEY_WOW6432KEY
    };
    for (size_t index = 0; index < _countof(views); ++index) {
        wchar_t install_root[2048] = {0};
        DWORD size = sizeof(install_root);
        LSTATUS result = RegGetValueW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\BongoCat",
            L"InstallLocation", RRF_RT_REG_SZ | views[index], NULL,
            install_root, &size);
        if (result != ERROR_SUCCESS || !install_root[0]) continue;
        size_t length = wcslen(install_root);
        while (length && (install_root[length - 1] == L'\\' ||
            install_root[length - 1] == L'/')) install_root[--length] = L'\0';
        static const wchar_t executable[] = L"\\BongoCat.exe";
        if (length + _countof(executable) > _countof(install_root)) continue;
        memcpy(install_root + length, executable, sizeof(executable));
        if (_wcsicmp(running, install_root) == 0) return true;
    }
    return false;
}

const char *bongo_cat_update_platform_asset(void) {
#ifdef _WIN64
    return "windows-x64";
#else
    return "windows-x86";
#endif
}

#else

bool bongo_cat_update_platform_supported(void) { return false; }
bool bongo_cat_update_platform_store(void) { return false; }
bool bongo_cat_update_platform_installed(void) { return false; }
const char *bongo_cat_update_platform_asset(void) { return "unsupported"; }

#endif
