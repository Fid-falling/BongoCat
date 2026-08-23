#include "bongo_cat/platform.h"

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_properties.h>

void bongo_cat_platform_configure_preferences_window(SDL_Window *sdl_window) {
    if (!sdl_window) return;
    NSWindow *window = (__bridge NSWindow *)SDL_GetPointerProperty(
        SDL_GetWindowProperties(sdl_window),
        SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    if (!window) return;
    NSWindowStyleMask style = [window styleMask];
    style |= NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
        NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable |
        NSWindowStyleMaskFullSizeContentView;
    [window setStyleMask:style];
    [window setTitle:@""];
    [window setTitleVisibility:NSWindowTitleHidden];
    [window setTitlebarAppearsTransparent:YES];
    [window setToolbar:nil];
    [window setTabbingMode:NSWindowTabbingModeDisallowed];
    [window setMovableByWindowBackground:NO];
    [[window standardWindowButton:NSWindowCloseButton] setHidden:NO];
    [[window standardWindowButton:NSWindowMiniaturizeButton] setHidden:NO];
    [[window standardWindowButton:NSWindowZoomButton] setHidden:NO];
}
#endif
