#include "windows_mouse_passthrough.h"

#ifdef _WIN32
#define CHILD_FLAGS (CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT)

static LPARAM point_parameter(POINT point) {
    return MAKELPARAM((short)point.x, (short)point.y);
}

static bool contains_point(HWND window, POINT point) {
    RECT bounds;
    if (!IsWindowVisible(window) || !IsWindowEnabled(window) ||
        !GetWindowRect(window, &bounds) || !PtInRect(&bounds, point)) return false;
    LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if (style & WS_EX_TRANSPARENT) return false;
    HRGN region = CreateRectRgn(0, 0, 0, 0);
    if (!region) return true;
    int type = GetWindowRgn(window, region);
    bool inside = type == ERROR || (type != NULLREGION &&
        PtInRegion(region, point.x - bounds.left, point.y - bounds.top));
    DeleteObject(region);
    return inside;
}

static HWND underlying_root(HWND overlay, POINT point) {
    RECT overlay_bounds;
    if (!GetWindowRect(overlay, &overlay_bounds) ||
        !PtInRect(&overlay_bounds, point)) return NULL;
    for (HWND window = GetTopWindow(NULL); window && window != overlay;
        window = GetWindow(window, GW_HWNDNEXT))
        if (contains_point(window, point)) return NULL;
    DWORD process = GetCurrentProcessId();
    for (HWND window = GetWindow(overlay, GW_HWNDNEXT); window;
        window = GetWindow(window, GW_HWNDNEXT)) {
        DWORD owner = 0;
        GetWindowThreadProcessId(window, &owner);
        if (owner != process && contains_point(window, point)) return window;
    }
    return NULL;
}

static HWND deepest_child(HWND root, POINT point) {
    HWND target = root;
    for (int depth = 0; depth < 16; ++depth) {
        POINT local = point;
        if (!ScreenToClient(target, &local)) break;
        HWND child = ChildWindowFromPointEx(target, local, CHILD_FLAGS);
        if (!child || child == target) break;
        target = child;
    }
    return target;
}

static WORD key_state(WPARAM message, DWORD mouse_data) {
    WORD keys = 0;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) keys |= MK_SHIFT;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) keys |= MK_CONTROL;
    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) keys |= MK_LBUTTON;
    if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) keys |= MK_RBUTTON;
    if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) keys |= MK_MBUTTON;
    if (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) keys |= MK_XBUTTON1;
    if (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) keys |= MK_XBUTTON2;
    if (message == WM_LBUTTONDOWN) keys |= MK_LBUTTON;
    else if (message == WM_LBUTTONUP) keys &= ~MK_LBUTTON;
    else if (message == WM_RBUTTONDOWN) keys |= MK_RBUTTON;
    else if (message == WM_RBUTTONUP) keys &= ~MK_RBUTTON;
    else if (message == WM_MBUTTONDOWN) keys |= MK_MBUTTON;
    else if (message == WM_MBUTTONUP) keys &= ~MK_MBUTTON;
    else if (message == WM_XBUTTONDOWN)
        keys |= HIWORD(mouse_data) == XBUTTON1 ? MK_XBUTTON1 : MK_XBUTTON2;
    else if (message == WM_XBUTTONUP)
        keys &= ~(HIWORD(mouse_data) == XBUTTON1 ? MK_XBUTTON1 : MK_XBUTTON2);
    return keys;
}

static bool button_down(WPARAM message) {
    return message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
        message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN;
}

static UINT nonclient_message(WPARAM message) {
    switch (message) {
    case WM_MOUSEMOVE: return WM_NCMOUSEMOVE;
    case WM_LBUTTONDOWN: return WM_NCLBUTTONDOWN;
    case WM_LBUTTONUP: return WM_NCLBUTTONUP;
    case WM_RBUTTONDOWN: return WM_NCRBUTTONDOWN;
    case WM_RBUTTONUP: return WM_NCRBUTTONUP;
    case WM_MBUTTONDOWN: return WM_NCMBUTTONDOWN;
    case WM_MBUTTONUP: return WM_NCMBUTTONUP;
    case WM_XBUTTONDOWN: return WM_NCXBUTTONDOWN;
    case WM_XBUTTONUP: return WM_NCXBUTTONUP;
    default: return 0;
    }
}

static LRESULT hit_test(HWND window, POINT point) {
    DWORD_PTR result = HTCLIENT;
    if (!SendMessageTimeoutW(window, WM_NCHITTEST, 0, point_parameter(point),
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 50, &result)) return HTCLIENT;
    return (LRESULT)result;
}

static bool activate(HWND root, LRESULT hit, WPARAM message) {
    DWORD_PTR result = MA_ACTIVATE;
    SendMessageTimeoutW(root, WM_MOUSEACTIVATE, (WPARAM)root,
        MAKELPARAM((WORD)hit, (WORD)message),
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 50, &result);
    if (result == MA_ACTIVATE || result == MA_ACTIVATEANDEAT)
        SetForegroundWindow(root);
    return result != MA_ACTIVATEANDEAT && result != MA_NOACTIVATEANDEAT;
}

bool bongo_cat_windows_mouse_passthrough(HWND overlay, WPARAM message,
    const MSLLHOOKSTRUCT *mouse) {
    /* HTTRANSPARENT only forwards reliably within one UI thread. */
    if (!overlay || !mouse) return false;
    POINT point = mouse->pt;
    HWND root = underlying_root(overlay, point);
    if (!root) return false;
    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
        WPARAM state = MAKEWPARAM(key_state(message, mouse->mouseData),
            HIWORD(mouse->mouseData));
        return PostMessageW(root, (UINT)message, state,
            point_parameter(point)) != FALSE;
    }
    if (message == WM_MOUSEMOVE) {
        HWND target = deepest_child(root, point);
        POINT local = point;
        if (!ScreenToClient(target, &local)) return false;
        return PostMessageW(target, WM_MOUSEMOVE,
            key_state(message, mouse->mouseData), point_parameter(local)) != FALSE;
    }
    LRESULT hit = hit_test(root, point);
    if (button_down(message) && !activate(root, hit, message)) return true;
    UINT nonclient = hit == HTCLIENT ? 0 : nonclient_message(message);
    if (nonclient) return PostMessageW(root, nonclient, (WPARAM)hit,
        point_parameter(point)) != FALSE;
    HWND target = deepest_child(root, point);
    POINT local = point;
    if (!ScreenToClient(target, &local)) return false;
    WPARAM state = key_state(message, mouse->mouseData);
    if (message == WM_XBUTTONDOWN || message == WM_XBUTTONUP)
        state = MAKEWPARAM((WORD)state, HIWORD(mouse->mouseData));
    return PostMessageW(target, (UINT)message, state,
        point_parameter(local)) != FALSE;
}
#endif
