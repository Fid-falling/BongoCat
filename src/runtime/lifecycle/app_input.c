#include "runtime.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"
#include "bongo_cat/preferences.h"

#include <stdio.h>
#include <string.h>

void bongo_cat_app_drain_input(BongoCatApp *app, bool allow_shortcuts) {
    if (app && app->secondary_pet) allow_shortcuts = false;
    BongoCatInputEvent event;
    while (bongo_cat_input_pop(&app->input, &event)) {
        if (app->smoke_ignore_global_input) continue;
        if (app->smoke_input_audit) {
            char path[BONGO_CAT_PATH_CAP];
            bongo_cat_path_join(path, sizeof(path), app->state_root,
                "input-audit.txt");
            FILE *file = bongo_cat_file_open(path, "ab");
            if (file) {
                fprintf(file, "kind=%d name=%s value=%.3f\n",
                    event.kind, event.name, event.value);
                fclose(file);
            }
        }
        if (strcmp(event.name, "CapsLock") == 0)
            bongo_cat_input_schedule_release(&app->input, &event, 100);
        if (allow_shortcuts &&
            !bongo_cat_preferences_shortcuts_blocked(app->preferences))
            bongo_cat_app_shortcuts(app, &event);
        bongo_cat_app_apply_input(app, &event);
    }
    uint64_t now = SDL_GetTicks();
    while (bongo_cat_input_take_scheduled_release(&app->input, now, &event)) {
        if (allow_shortcuts &&
            !bongo_cat_preferences_shortcuts_blocked(app->preferences))
            bongo_cat_app_shortcuts(app, &event);
        bongo_cat_app_apply_input(app, &event);
    }
    if (!app->smoke_ignore_global_input) bongo_cat_app_apply_mouse(app);
}
