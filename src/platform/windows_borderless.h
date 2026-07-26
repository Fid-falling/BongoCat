#ifndef BONGO_CAT_NEO_WINDOWS_BORDERLESS_H
#define BONGO_CAT_NEO_WINDOWS_BORDERLESS_H

#ifdef _WIN32
#include "bongo_cat_neo/platform.h"
#include <windows.h>

void bongo_cat_neo_windows_borderless_install(HWND window);
void bongo_cat_neo_windows_borderless_uninstall(HWND window);
void bongo_cat_neo_windows_menu_preview(HWND window, BongoCatNeoMenuPreview preview,
    void (*tick)(void *userdata), void *userdata);
#endif

#endif
