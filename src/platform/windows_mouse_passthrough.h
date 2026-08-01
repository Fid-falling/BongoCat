#ifndef BONGO_CAT_WINDOWS_MOUSE_PASSTHROUGH_H
#define BONGO_CAT_WINDOWS_MOUSE_PASSTHROUGH_H

#ifdef _WIN32
#include <stdbool.h>
#include <windows.h>

bool bongo_cat_windows_mouse_passthrough(HWND overlay, WPARAM message,
    const MSLLHOOKSTRUCT *mouse);
#endif

#endif
