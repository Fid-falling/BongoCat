#include "bongo_cat_neo/path.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include "windows_utf8.h"
#include <stdlib.h>
#include <windows.h>
#else
#include <dirent.h>
#endif

bool bongo_cat_neo_path_join(char *out, size_t cap, const char *left, const char *right) {
    if (!out || !cap || !left || !right) return false;
    size_t len = strlen(left);
    char sep = '/';
    bool has_sep = len > 0 && (left[len - 1] == '/' || left[len - 1] == '\\');
    int count = snprintf(out, cap, "%s%s%s", left, has_sep ? "" : (char[]){sep, 0}, right);
    return count >= 0 && (size_t)count < cap;
}

const char *bongo_cat_neo_path_name(const char *path) {
    if (!path) return "";
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *name = slash > backslash ? slash : backslash;
    return name ? name + 1 : path;
}

static bool path_type(const char *path, bool directory) {
#ifdef _WIN32
    wchar_t *wide = bongo_cat_neo_windows_wide(path);
    struct _stat64 value = {0};
    bool found = wide && _wstat64(wide, &value) == 0;
    free(wide);
    return found && (directory ? (value.st_mode & _S_IFDIR) != 0
        : (value.st_mode & _S_IFREG) != 0);
#else
    struct stat value;
    if (!path || stat(path, &value) != 0) return false;
    return directory ? S_ISDIR(value.st_mode) : S_ISREG(value.st_mode);
#endif
}

bool bongo_cat_neo_path_is_file(const char *path) { return path_type(path, false); }
bool bongo_cat_neo_path_is_dir(const char *path) { return path_type(path, true); }

static bool ends_with(const char *text, const char *suffix) {
    size_t a = strlen(text), b = strlen(suffix);
    return a >= b && strcmp(text + a - b, suffix) == 0;
}

bool bongo_cat_neo_path_find_suffix(const char *dir, const char *suffix, char *name, size_t cap) {
#ifdef _WIN32
    char pattern[BONGO_CAT_NEO_PATH_CAP];
    if (!bongo_cat_neo_path_join(pattern, sizeof(pattern), dir, "*")) return false;
    wchar_t *wide = bongo_cat_neo_windows_wide(pattern);
    WIN32_FIND_DATAW data = {0};
    HANDLE find = wide ? FindFirstFileW(wide, &data) : INVALID_HANDLE_VALUE;
    free(wide);
    if (find == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    do {
        char filename[BONGO_CAT_NEO_PATH_CAP];
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            bongo_cat_neo_windows_utf8(data.cFileName, filename, sizeof(filename)) &&
            ends_with(filename, suffix)) {
            snprintf(name, cap, "%s", filename);
            found = true;
            break;
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return found;
#else
    DIR *handle = opendir(dir);
    if (!handle) return false;
    bool found = false;
    struct dirent *entry;
    while ((entry = readdir(handle))) {
        if (ends_with(entry->d_name, suffix)) {
            snprintf(name, cap, "%s", entry->d_name);
            found = true;
            break;
        }
    }
    closedir(handle);
    return found;
#endif
}

int bongo_cat_neo_path_find_unique_suffix(const char *dir, const char *suffix,
    char *name, size_t cap) {
#ifdef _WIN32
    char pattern[BONGO_CAT_NEO_PATH_CAP];
    if (!bongo_cat_neo_path_join(pattern, sizeof(pattern), dir, "*")) return 0;
    wchar_t *wide = bongo_cat_neo_windows_wide(pattern);
    WIN32_FIND_DATAW data = {0};
    HANDLE find = wide ? FindFirstFileW(wide, &data) : INVALID_HANDLE_VALUE;
    free(wide);
    if (find == INVALID_HANDLE_VALUE) return 0;
    int count = 0;
    do {
        char filename[BONGO_CAT_NEO_PATH_CAP];
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            bongo_cat_neo_windows_utf8(data.cFileName, filename, sizeof(filename)) &&
            ends_with(filename, suffix)) {
            if (++count == 1) snprintf(name, cap, "%s", filename);
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return count == 1 ? 1 : count ? -1 : 0;
#else
    DIR *handle = opendir(dir);
    if (!handle) return 0;
    int count = 0; struct dirent *entry;
    while ((entry = readdir(handle))) if (ends_with(entry->d_name, suffix)) {
        char path[BONGO_CAT_NEO_PATH_CAP];
        if (!bongo_cat_neo_path_join(path, sizeof(path), dir, entry->d_name) ||
            !bongo_cat_neo_path_is_file(path)) continue;
        if (++count == 1) snprintf(name, cap, "%s", entry->d_name);
    }
    closedir(handle);
    return count == 1 ? 1 : count ? -1 : 0;
#endif
}
