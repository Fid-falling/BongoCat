#include "bongo_cat_neo/platform.h"
#include "macos_internal.h"

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_properties.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

static int instance_lock = -1;
static BongoCatNeoPlatform *active_platform;
static NSObject *instance_observer;
static bool instance_show_pending;

/* SDL 3.2's pinned Cocoa tray layout; kept local to the platform adapter. */
typedef struct BongoCatNeoSDLTrayMenu { NSMenu *menu; } BongoCatNeoSDLTrayMenu;
typedef struct BongoCatNeoSDLTray {
    NSStatusBar *status_bar; NSStatusItem *status_item; BongoCatNeoSDLTrayMenu *menu;
} BongoCatNeoSDLTray;

@interface BongoCatNeoTrayTarget : NSObject {
    NSStatusItem *item_; NSMenu *menu_; BongoCatNeoTrayClick callback_; void *userdata_;
}
- (id)initWithItem:(NSStatusItem *)item menu:(NSMenu *)menu
    callback:(BongoCatNeoTrayClick)callback userdata:(void *)userdata;
- (void)clicked:(id)sender;
- (void)unbind;
@end

@implementation BongoCatNeoTrayTarget
- (id)initWithItem:(NSStatusItem *)item menu:(NSMenu *)menu
    callback:(BongoCatNeoTrayClick)callback userdata:(void *)userdata {
    self = [super init];
    if (self) { item_ = item; menu_ = menu; callback_ = callback; userdata_ = userdata; }
    return self;
}
- (void)clicked:(id)sender {
    (void)sender;
    NSEvent *event = [NSApp currentEvent];
    if ([event type] == NSEventTypeLeftMouseUp) callback_(userdata_);
    else [NSMenu popUpContextMenu:menu_ withEvent:event forView:[item_ button]];
}
- (void)unbind {
    [[item_ button] setTarget:nil]; [[item_ button] setAction:nil];
    [item_ setMenu:menu_];
}
@end

static BongoCatNeoTrayTarget *tray_target;

static NSWindow *native_window(BongoCatNeoPlatform *platform) {
    return (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(platform->window),
        SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
}

static void show_instance(void) {
    if (!active_platform) { instance_show_pending = true; return; }
    [native_window(active_platform) orderFrontRegardless];
    [NSApp activateIgnoringOtherApps:YES];
    instance_show_pending = false;
}

@interface BongoCatNeoInstanceObserver : NSObject
- (void)showWindow:(NSNotification *)notification;
@end
@implementation BongoCatNeoInstanceObserver
- (void)showWindow:(NSNotification *)notification {
    if (![NSThread isMainThread]) {
        [self performSelectorOnMainThread:@selector(showWindow:)
            withObject:notification waitUntilDone:NO];
        return;
    }
    show_instance();
}
@end

static void observe_instance(void) {
    if (instance_observer) return;
    instance_observer = [[BongoCatNeoInstanceObserver alloc] init];
    [[NSDistributedNotificationCenter defaultCenter] addObserver:instance_observer
        selector:@selector(showWindow:) name:@"com.bongocatneo.desktop.show" object:nil
        suspensionBehavior:NSNotificationSuspensionBehaviorDeliverImmediately];
}
BongoCatNeoResult bongo_cat_neo_platform_init(BongoCatNeoPlatform *platform, SDL_Window *window,
    BongoCatNeoInputState *input, BongoCatNeoError *error) {
    (void)error;
    memset(platform, 0, sizeof(*platform));
    platform->window = window;
    platform->input = input;
    active_platform = platform;
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    BongoCatNeoError input_error = {0};
    if (!bongo_cat_neo_macos_input_start(platform, &input_error))
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", input_error.message);
    if (instance_show_pending) show_instance();
    return BONGO_CAT_NEO_OK;
}
void bongo_cat_neo_platform_shutdown(BongoCatNeoPlatform *platform) {
    bongo_cat_neo_macos_input_stop(platform);
    if (active_platform == platform) active_platform = NULL;
}
void bongo_cat_neo_platform_set_click_through(BongoCatNeoPlatform *platform, bool enabled) {
    [native_window(platform) setIgnoresMouseEvents:enabled];
}
bool bongo_cat_neo_platform_pointer_local(BongoCatNeoPlatform *platform, double screen_x,
    double screen_y, float *local_x, float *local_y) {
    int x, y, width, height;
    if (!platform || !local_x || !local_y ||
        !SDL_GetWindowPosition(platform->window, &x, &y) ||
        !SDL_GetWindowSize(platform->window, &width, &height)) return false;
    *local_x = (float)(screen_x - x); *local_y = (float)(screen_y - y);
    return *local_x >= 0 && *local_x < width && *local_y >= 0 && *local_y < height;
}
void bongo_cat_neo_platform_set_always_on_top(BongoCatNeoPlatform *platform, bool enabled) {
    [native_window(platform) setLevel:enabled ? NSFloatingWindowLevel : NSNormalWindowLevel];
}
void bongo_cat_neo_platform_set_taskbar(BongoCatNeoPlatform *platform, bool visible) {
    (void)platform;
    [NSApp setActivationPolicy:visible ? NSApplicationActivationPolicyRegular :
        NSApplicationActivationPolicyAccessory];
}
void bongo_cat_neo_platform_raise_window(SDL_Window *window) {
    if (!window) return;
    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
    NSWindow *native = (__bridge NSWindow *)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    [NSApp activateIgnoringOtherApps:YES];
    [native makeKeyAndOrderFront:nil];
}

bool bongo_cat_neo_platform_set_geometry(BongoCatNeoPlatform *platform,
    int x, int y, int width, int height) {
    if (!platform || !platform->window) return false;
    int current_width, current_height;
    if ((!SDL_GetWindowSize(platform->window, &current_width, &current_height) ||
        current_width != width || current_height != height) &&
        !SDL_SetWindowSize(platform->window, width, height)) return false;
    int current_x, current_y;
    if (!SDL_GetWindowPosition(platform->window, &current_x, &current_y) ||
        current_x != x || current_y != y)
        SDL_SetWindowPosition(platform->window, x, y);
    return true;
}
void bongo_cat_neo_platform_begin_drag(BongoCatNeoPlatform *platform) {
    NSWindow *window = native_window(platform);
    [window performWindowDragWithEvent:[NSApp currentEvent]];
}
bool bongo_cat_neo_platform_dynamic_hit_supported(void) {
    return bongo_cat_neo_macos_input_supported();
}

void bongo_cat_neo_platform_set_tray_left_click(void *tray, BongoCatNeoTrayClick callback,
    void *userdata) {
    BongoCatNeoSDLTray *native = tray;
    if (tray_target) {
        [tray_target unbind]; [tray_target release]; tray_target = nil;
    }
    if (!native || !callback || !native->status_item || !native->menu) return;
    tray_target = [[BongoCatNeoTrayTarget alloc] initWithItem:native->status_item
        menu:native->menu->menu callback:callback userdata:userdata];
    [native->status_item setMenu:nil];
    [[native->status_item button] setTarget:tray_target];
    [[native->status_item button] setAction:@selector(clicked:)];
    [[native->status_item button] sendActionOn:NSEventMaskLeftMouseUp |
        NSEventMaskRightMouseUp];
}
bool bongo_cat_neo_platform_single_instance_begin(void) {
    char path[96]; snprintf(path, sizeof(path), "/tmp/bongo-cat-neo-%lu.lock",
        (unsigned long)getuid());
    instance_lock = open(path, O_CREAT | O_RDWR, 0600);
    if (instance_lock < 0 || flock(instance_lock, LOCK_EX | LOCK_NB) == 0) {
        observe_instance(); return true;
    }
    [[NSDistributedNotificationCenter defaultCenter]
        postNotificationName:@"com.bongocatneo.desktop.show" object:nil
        userInfo:nil deliverImmediately:YES];
    close(instance_lock); instance_lock = -1; return false;
}
void bongo_cat_neo_platform_single_instance_end(void) {
    if (instance_lock >= 0) close(instance_lock);
    instance_lock = -1;
    if (instance_observer) {
        [[NSDistributedNotificationCenter defaultCenter] removeObserver:instance_observer];
        [instance_observer release]; instance_observer = nil;
    }
}
BongoCatNeoResult bongo_cat_neo_platform_set_autostart(bool enabled, BongoCatNeoError *error) {
    @autoreleasepool {
        NSString *directory = [NSHomeDirectory()
            stringByAppendingPathComponent:@"Library/LaunchAgents"];
        NSString *path = [directory
            stringByAppendingPathComponent:@"com.bongocatneo.desktop.plist"];
        NSFileManager *files = [NSFileManager defaultManager];
        if (!enabled) {
            if (![files fileExistsAtPath:path] || [files removeItemAtPath:path error:nil])
                return BONGO_CAT_NEO_OK;
            bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_IO, "Cannot remove macOS launch agent");
            return BONGO_CAT_NEO_ERROR_IO;
        }
        NSString *executable = [[NSBundle mainBundle] executablePath];
        if (!executable || ![files createDirectoryAtPath:directory
            withIntermediateDirectories:YES attributes:nil error:nil]) {
            bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_IO, "Cannot create macOS launch agent directory");
            return BONGO_CAT_NEO_ERROR_IO;
        }
        NSDictionary *plist = @{ @"Label": @"com.bongocatneo.desktop",
            @"ProgramArguments": @[executable, @"--autostart"], @"RunAtLoad": @YES };
        if ([plist writeToFile:path atomically:YES]) return BONGO_CAT_NEO_OK;
        bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_IO, "Cannot write macOS launch agent");
        return BONGO_CAT_NEO_ERROR_IO;
    }
}
BongoCatNeoMenuAction bongo_cat_neo_platform_context_menu(BongoCatNeoPlatform *platform,
    const BongoCatNeoMenuLabels *labels) {
    return bongo_cat_neo_macos_context_menu(platform, labels);
}
BongoCatNeoResult bongo_cat_neo_platform_embedded_assets(const char *target, BongoCatNeoError *error) {
    (void)target; (void)error; return BONGO_CAT_NEO_ERROR_PLATFORM;
}
#endif
