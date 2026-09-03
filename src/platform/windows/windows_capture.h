#ifndef BONGO_CAT_WINDOWS_CAPTURE_H
#define BONGO_CAT_WINDOWS_CAPTURE_H

#ifdef _WIN32
#include <stdbool.h>
#include <windows.h>

bool bongo_cat_windows_capture_configure(HWND window);
/* Mark windows whose OpenGL back buffer relies on DWM alpha composition. */
void bongo_cat_windows_capture_mark_transparent(HWND window, bool enabled);
/* Install a small native shim for synchronous DWM/shell notifications. */
void bongo_cat_windows_capture_install_transparency_handler(HWND window);
bool bongo_cat_windows_capture_restore_transparency(HWND window);
void bongo_cat_windows_capture_log(HWND window, const char *stage);
bool bongo_cat_windows_capture_handle_message(
    HWND window, UINT message, WPARAM wparam);
#endif

#endif
