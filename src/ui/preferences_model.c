#include "preferences_state.h"
#include "preferences_model_card.h"
#include "preferences_model_cover.h"
#include "preferences_notice.h"
#include "preferences_widgets.h"
#include "bongo_cat/i18n.h"
#include "bongo_cat/preferences.h"

#include <stdio.h>
#include <string.h>

#define MODEL_CARD_HEIGHT 214
#define MODEL_LOAD_REPORTED_LIMIT .60f

static const char *tr(BongoCatApp *app, const char *key,
    const char *fallback) {
    return bongo_cat_i18n_get(app->i18n, key, fallback);
}

void bongo_cat_preferences_model_load_progress(BongoCatPreferences *value,
    float progress) {
    if (!value || !value->model_loading) return;
    value->model_load_progress = NK_CLAMP(0.0f,
        progress * MODEL_LOAD_REPORTED_LIMIT, MODEL_LOAD_REPORTED_LIMIT);
    value->render_dirty = true;
    uint64_t now = SDL_GetTicksNS();
    bool due = progress >= .999f ||
        progress - value->model_load_render_progress >= .02f ||
        now - value->model_load_render_ns >= 16000000ull;
    if (value->window && due) {
        value->model_load_render_progress = progress;
        value->model_load_render_ns = now;
        bongo_cat_preferences_render(value);
    }
}

static void finish_model_load_progress(BongoCatPreferences *value) {
    if (!value || !value->window) {
        if (value) value->model_load_progress = 1.0f;
        return;
    }
    float start = value->model_load_progress;
    for (int i = 1; i <= 10; ++i) {
        float amount = (float)i / 10.0f;
        value->model_load_progress = start + (1.0f - start) * amount;
        value->render_dirty = true;
        bongo_cat_preferences_render(value);
        if (i < 10) SDL_Delay(8);
    }
}

void bongo_cat_preferences_process_model_selection(BongoCatPreferences *value) {
    if (!value || !value->model_selection_pending || value->model_loading)
        return;
    char id[BONGO_CAT_ID_CAP];
    snprintf(id, sizeof(id), "%s", value->pending_model_id);
    value->pending_model_id[0] = '\0';
    value->model_selection_pending = false;
    value->model_loading = true;
    value->model_load_progress = 0.0f;
    value->model_load_render_progress = 0.0f;
    value->model_load_render_ns = 0;
    snprintf(value->loading_model_id, sizeof(value->loading_model_id), "%s", id);
    BongoCatError error = {0};
    bool selected = bongo_cat_app_select_model_with_error(value->app, id, &error);
    if (selected) finish_model_load_progress(value);
    else value->model_load_progress = 0.0f;
    value->model_loading = false;
    value->loading_model_id[0] = '\0';
    if (!selected) bongo_cat_preferences_notice_show(value->app, tr(value->app,
        "native.modelLoadFailed", "Unable to display this model"), true);
    bongo_cat_preferences_invalidate(value);
    if (value->window) bongo_cat_preferences_render(value);
}

void bongo_cat_preferences_import_complete(BongoCatApp *app,
    BongoCatResult result, const BongoCatError *error, size_t resolved_count,
    size_t installed_count) {
    if (!app || !app->preferences) return;
    bool partial = result == BONGO_CAT_OK && error && error->message[0];
    const char *message = result != BONGO_CAT_OK || partial
        ? (error && error->message[0] ? error->message : "Model import failed")
        : installed_count ? tr(app,
            "pages.preference.model.hints.importSuccess", "Model imported")
        : tr(app, "pages.preference.model.hints.importExists",
            "Model already exists");
    if (app->smoke) {
        if (result != BONGO_CAT_OK || partial) app->exit_code = 1;
    } else bongo_cat_preferences_notice_show(app, message,
        result != BONGO_CAT_OK || partial);
    if (result == BONGO_CAT_OK) {
        if (installed_count)
            bongo_cat_preferences_reload_fonts(app->preferences);
        SDL_Log("Resolved %zu model package(s); installed %zu new package(s)",
            resolved_count, installed_count);
    }
    bongo_cat_preferences_invalidate(app->preferences);
    bongo_cat_preferences_render(app->preferences);
}

static void smoke_model_behavior(BongoCatPreferences *value) {
    BongoCatApp *app = value->app;
    if (app->smoke_preference_model_select) {
        for (size_t i = 0; i < app->models.count; ++i) {
            const BongoCatModelEntry *entry = &app->models.entries[i];
            if (entry->preset || !strcmp(entry->id,
                app->session.active_model_id)) continue;
            app->smoke_preference_model_select = false;
            value->smoke_behavior_open_pending = true;
            SDL_Log("Preferences smoke selecting model %s", entry->id);
            bongo_cat_preferences_model_select(value, entry);
            return;
        }
    }
    if (value->smoke_behavior_open_pending && !value->font_reload_pending &&
        !value->model_selection_pending && !value->model_loading) {
        value->smoke_behavior_open_pending = false;
        bongo_cat_preferences_behavior_dialog_open(value);
    }
}

static size_t model_count(const BongoCatPreferences *value, bool managed) {
    size_t count = 0;
    for (size_t i = 0; i < value->app->models.count; ++i)
        if (value->app->models.entries[i].managed == managed) count++;
    return count;
}

static void draw_models(BongoCatPreferences *value,
    struct nk_context *context, bool managed) {
    for (size_t i = 0; i < value->app->models.count; ++i) {
        const BongoCatModelEntry *entry = &value->app->models.entries[i];
        if (entry->managed != managed) continue;
        bongo_cat_preferences_model_card(value, context, entry);
    }
}

void bongo_cat_preferences_page_model(BongoCatPreferences *value,
    struct nk_context *context) {
    BongoCatApp *app = value->app;
    smoke_model_behavior(value);
    bongo_cat_preferences_model_covers_begin(app);
    bongo_cat_pref_section(context,
        tr(app, "pages.preference.model.title", "Installed models"));
    float width = nk_window_get_content_region(context).w;
    int columns = width >= 780 ? 4 : width >= 620 ? 3 : width >= 400 ? 2 : 1;
    struct nk_vec2 old_spacing = context->style.window.spacing;
    context->style.window.spacing = nk_vec2(14, 17);
    nk_layout_row_dynamic(context, MODEL_CARD_HEIGHT, columns);
    if (bongo_cat_preferences_model_import_card(value, context))
        bongo_cat_preferences_request_model_import(app->preferences);
    draw_models(value, context, false);
    if (model_count(value, true)) {
        bongo_cat_pref_section(context,
            tr(app, "pages.preference.model.nearbyTitle", "Nearby models"));
        nk_layout_row_dynamic(context, MODEL_CARD_HEIGHT, columns);
        draw_models(value, context, true);
    }
    context->style.window.spacing = old_spacing;
    bongo_cat_preferences_model_covers_prune(app);
    if (!value->model_loading &&
        bongo_cat_preferences_model_cover_generate_current(app))
        bongo_cat_preferences_invalidate(value);
}
