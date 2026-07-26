#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "bongo_cat_neo/platform.h"
#include "bongo_cat_neo/path.h"
#include "linux_internal.h"

#if !defined(_WIN32) && !defined(__APPLE__)
#include <SDL3/SDL.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>
#include <X11/Xlib.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>

static int instance_lock = -1;
static BongoCatNeoPlatform *active_platform;

static void publish_instance_window(SDL_Window *window) {
    if (instance_lock < 0 || !window) return;
    Window id = (Window)SDL_GetNumberProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    if (!id) return;
    char text[32]; int length = snprintf(text, sizeof(text), "%lu\n", (unsigned long)id);
    if (length <= 0 || (size_t)length >= sizeof(text)) return;
    if (ftruncate(instance_lock, 0) == 0 && lseek(instance_lock, 0, SEEK_SET) == 0) {
        if (write(instance_lock, text, (size_t)length) == (ssize_t)length) fsync(instance_lock);
    }
}

static void restore_instance_window(void) {
    if (instance_lock < 0) return;
    char text[32] = {0}; lseek(instance_lock, 0, SEEK_SET);
    ssize_t length = read(instance_lock, text, sizeof(text) - 1);
    Window window = length > 0 ? (Window)strtoul(text, NULL, 10) : 0;
    Display *display = window ? XOpenDisplay(NULL) : NULL;
    if (!display) return;
    XMapRaised(display, window);
    Atom active = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
    XEvent event = {0}; event.xclient.type = ClientMessage;
    event.xclient.window = window; event.xclient.message_type = active;
    event.xclient.format = 32; event.xclient.data.l[0] = 2;
    XSendEvent(display, DefaultRootWindow(display), False,
        SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XFlush(display); XCloseDisplay(display);
}

static bool executable_path(char output[BONGO_CAT_NEO_PATH_CAP]) {
    const char *appimage = getenv("APPIMAGE");
    if (appimage && appimage[0]) {
        int length = snprintf(output, BONGO_CAT_NEO_PATH_CAP, "%s", appimage);
        return length >= 0 && length < BONGO_CAT_NEO_PATH_CAP;
    }
    ssize_t length = readlink("/proc/self/exe", output, BONGO_CAT_NEO_PATH_CAP - 1);
    if (length <= 0 || length >= BONGO_CAT_NEO_PATH_CAP) return false;
    output[length] = '\0'; return true;
}

BongoCatNeoResult bongo_cat_neo_platform_init(BongoCatNeoPlatform *platform, SDL_Window *window,
    BongoCatNeoInputState *input, BongoCatNeoError *error) {
    (void)error;
    memset(platform, 0, sizeof(*platform));
    platform->window = window;
    platform->input = input;
    active_platform = platform;
    publish_instance_window(window);
    BongoCatNeoError input_error = {0};
    if (!bongo_cat_neo_linux_x11_start(platform, &input_error) && input_error.message[0])
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", input_error.message);
    return BONGO_CAT_NEO_OK;
}
void bongo_cat_neo_platform_shutdown(BongoCatNeoPlatform *platform) {
    bongo_cat_neo_linux_x11_stop(platform);
    if (active_platform == platform) active_platform = NULL;
}
void bongo_cat_neo_platform_set_click_through(BongoCatNeoPlatform *platform, bool enabled) {
    bongo_cat_neo_linux_x11_click_through(platform, enabled);
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
    SDL_SetWindowAlwaysOnTop(platform->window, enabled);
}
void bongo_cat_neo_platform_set_taskbar(BongoCatNeoPlatform *platform, bool visible) {
    bongo_cat_neo_linux_x11_taskbar(platform, visible);
}
void bongo_cat_neo_platform_raise_window(SDL_Window *window) {
    if (!window) return;
    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
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
    bongo_cat_neo_linux_x11_begin_drag(platform);
}
bool bongo_cat_neo_platform_dynamic_hit_supported(void) {
    return bongo_cat_neo_linux_x11_supported(active_platform);
}

void bongo_cat_neo_platform_set_tray_left_click(void *tray, BongoCatNeoTrayClick callback,
    void *userdata) {
    (void)tray; (void)callback; (void)userdata;
}
bool bongo_cat_neo_platform_single_instance_begin(void) {
    char path[96]; snprintf(path, sizeof(path), "/tmp/bongo-cat-neo-%lu.lock",
        (unsigned long)getuid());
    instance_lock = open(path, O_CREAT | O_RDWR, 0600);
    if (instance_lock < 0 || flock(instance_lock, LOCK_EX | LOCK_NB) == 0) return true;
    restore_instance_window(); close(instance_lock); instance_lock = -1; return false;
}
void bongo_cat_neo_platform_single_instance_end(void) {
    if (instance_lock >= 0) close(instance_lock);
    instance_lock = -1;
}
BongoCatNeoResult bongo_cat_neo_platform_set_autostart(bool enabled, BongoCatNeoError *error) {
    const char *base = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    char config[BONGO_CAT_NEO_PATH_CAP], directory[BONGO_CAT_NEO_PATH_CAP], path[BONGO_CAT_NEO_PATH_CAP];
    if (base && base[0]) snprintf(config, sizeof(config), "%s", base);
    else if (home && bongo_cat_neo_path_join(config, sizeof(config), home, ".config")) {}
    else return BONGO_CAT_NEO_ERROR_PLATFORM;
    if (!bongo_cat_neo_path_join(directory, sizeof(directory), config, "autostart") ||
        !bongo_cat_neo_path_join(path, sizeof(path), directory, "bongo-cat-neo.desktop"))
        return BONGO_CAT_NEO_ERROR_IO;
    if (!enabled) {
        if (remove(path) == 0 || errno == ENOENT) return BONGO_CAT_NEO_OK;
        bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_IO, "Cannot remove Linux autostart entry");
        return BONGO_CAT_NEO_ERROR_IO;
    }
    char executable[BONGO_CAT_NEO_PATH_CAP];
    if (!executable_path(executable) || strchr(executable, '"') ||
        !bongo_cat_neo_path_create_directory(directory)) {
        bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_IO, "Cannot prepare Linux autostart entry");
        return BONGO_CAT_NEO_ERROR_IO;
    }
    FILE *file = fopen(path, "wb");
    if (!file) return BONGO_CAT_NEO_ERROR_IO;
    bool written = fprintf(file, "[Desktop Entry]\nType=Application\nName=Bongo Cat Neo\n"
        "Exec=\"%s\" --autostart\nTerminal=false\nX-GNOME-Autostart-enabled=true\n", executable) > 0;
    if (fclose(file) != 0) written = false;
    if (written) return BONGO_CAT_NEO_OK;
    remove(path); bongo_cat_neo_error_set(error, BONGO_CAT_NEO_ERROR_IO, "Cannot write Linux autostart entry");
    return BONGO_CAT_NEO_ERROR_IO;
}
BongoCatNeoMenuAction bongo_cat_neo_platform_context_menu(BongoCatNeoPlatform *platform,
    const BongoCatNeoMenuLabels *labels) {
    return bongo_cat_neo_linux_context_menu(platform, labels);
}
BongoCatNeoResult bongo_cat_neo_platform_embedded_assets(const char *target, BongoCatNeoError *error) {
    (void)target; (void)error; return BONGO_CAT_NEO_ERROR_PLATFORM;
}
#endif
