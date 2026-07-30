#include "runtime.h"

#define DISPLAY_RECOVERY_DELAY_NS 750000000ull

static bool window_rect(BongoCatApp *app, SDL_Rect *rect) {
    return app && app->window && rect &&
        SDL_GetWindowPosition(app->window, &rect->x, &rect->y) &&
        SDL_GetWindowSize(app->window, &rect->w, &rect->h) &&
        rect->w > 0 && rect->h > 0;
}

static bool intersects_bounds(const SDL_Rect *window,
    const SDL_Rect *bounds, int count) {
    if (!window || !bounds || count < 1) return false;
    for (int i = 0; i < count; ++i)
        if (SDL_HasRectIntersection(window, &bounds[i])) return true;
    return false;
}

static bool intersects_available_display(const SDL_Rect *window, bool *known) {
    int count = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&count);
    *known = displays && count > 0;
    bool intersects = false;
    for (int i = 0; displays && i < count && !intersects; ++i) {
        SDL_Rect bounds;
        if (SDL_GetDisplayBounds(displays[i], &bounds))
            intersects = SDL_HasRectIntersection(window, &bounds);
    }
    SDL_free(displays);
    return intersects;
}

static SDL_Point fitted_position(const SDL_Rect *window,
    const SDL_Rect *bounds) {
    int max_x = SDL_max(bounds->x, bounds->x + bounds->w - window->w);
    int max_y = SDL_max(bounds->y, bounds->y + bounds->h - window->h);
    SDL_Point point = {SDL_clamp(window->x, bounds->x, max_x),
        SDL_clamp(window->y, bounds->y, max_y)};
    return point;
}

static SDL_DisplayID target_display(BongoCatApp *app, const SDL_Rect *rect) {
    if (app->window_drag_active || app->resize_gesture) {
        float pointer_x = 0.0f, pointer_y = 0.0f;
        SDL_GetGlobalMouseState(&pointer_x, &pointer_y);
        SDL_Point pointer = {(int)pointer_x, (int)pointer_y};
        SDL_DisplayID display = SDL_GetDisplayForPoint(&pointer);
        if (display) return display;
    }
    SDL_DisplayID display = SDL_GetDisplayForWindow(app->window);
    if (!display && rect) display = SDL_GetDisplayForRect(rect);
    return display ? display : SDL_GetPrimaryDisplay();
}

static bool fit_to_display(BongoCatApp *app, SDL_DisplayID display,
    const SDL_Rect *window) {
    SDL_Rect bounds;
    if (!display || (!SDL_GetDisplayUsableBounds(display, &bounds) &&
        !SDL_GetDisplayBounds(display, &bounds))) return false;
    SDL_Point next = fitted_position(window, &bounds);
    if (next.x == window->x && next.y == window->y) return false;
    if (!SDL_SetWindowPosition(app->window, next.x, next.y)) return false;
    app->config.window.x = next.x;
    app->config.window.y = next.y;
    bongo_cat_window_mark_hit_dirty(app);
    return true;
}

void bongo_cat_window_clamp_to_display(BongoCatApp *app) {
    SDL_Rect rect;
    if (!app || !app->config.window.keep_in_screen || !window_rect(app, &rect)) return;
    fit_to_display(app, target_display(app, &rect), &rect);
}

bool bongo_cat_window_recover_to_display(BongoCatApp *app) {
    SDL_Rect rect;
    if (!window_rect(app, &rect)) return false;
    bool known = false;
    if (intersects_available_display(&rect, &known) || !known) return false;
    SDL_DisplayID display = SDL_GetDisplayForRect(&rect);
    if (!display) display = SDL_GetPrimaryDisplay();
    return fit_to_display(app, display, &rect);
}

void bongo_cat_window_display_event(BongoCatApp *app, const SDL_Event *event) {
    if (!app || !event || event->type == SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED)
        return;
    if (event->type < SDL_EVENT_DISPLAY_FIRST ||
        event->type > SDL_EVENT_DISPLAY_LAST) return;
    app->display_recovery_due_ns = SDL_GetTicksNS() + DISPLAY_RECOVERY_DELAY_NS;
}

void bongo_cat_window_update_display_recovery(BongoCatApp *app, uint64_t now) {
    if (!app || !app->display_recovery_due_ns ||
        now < app->display_recovery_due_ns) return;
    app->display_recovery_due_ns = 0;
    if (app->config.window.keep_in_screen) bongo_cat_window_clamp_to_display(app);
    else bongo_cat_window_recover_to_display(app);
}

bool bongo_cat_window_display_self_test(void) {
    const SDL_Rect displays[] = {{0, 0, 1920, 1040}, {1920, 0, 1920, 1040}};
    SDL_Rect partial = {-80, 100, 320, 240};
    SDL_Rect secondary = {2200, 100, 320, 240};
    SDL_Rect detached = {4200, 100, 320, 240};
    SDL_Point fitted = fitted_position(&detached, &displays[0]);
    bool overlap = intersects_bounds(&partial, displays, 2);
    bool second = intersects_bounds(&secondary, displays, 2);
    bool removed = !intersects_bounds(&secondary, displays, 1);
    bool recovered = fitted.x == 1600 && fitted.y == 100;
    return overlap && second && removed && recovered &&
        !intersects_bounds(&detached, displays, 2);
}
