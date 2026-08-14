#ifndef BONGO_CAT_WINDOWS_DIAGNOSTICS_H
#define BONGO_CAT_WINDOWS_DIAGNOSTICS_H

#ifdef _WIN32
#include <windows.h>

void bongo_cat_windows_diagnostics_log(HWND window);
#endif

#endif
