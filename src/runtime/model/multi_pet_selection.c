#include "runtime.h"

#include <string.h>

bool bongo_cat_app_model_active(const BongoCatApp *app, const char *id) {
    if (!app || !id) return false;
    if (!strcmp(app->session.active_model_id, id)) return true;
    return app->settings.model.multiple_pets &&
        bongo_cat_session_model_active(&app->session, id);
}

size_t bongo_cat_app_active_model_count(const BongoCatApp *app) {
    return app && app->settings.model.multiple_pets
        ? 1 + app->session.additional_model_count : app ? 1 : 0;
}
