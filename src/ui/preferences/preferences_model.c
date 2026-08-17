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
#define MODEL_LOAD_RENDER_INTERVAL_NS 33333333ull

static const char *tr(BongoCatApp *app, const char *key,
    const char *fallback) {
    return bongo_cat_i18n_get(app->i18n, key, fallback);
}

static float visual_wait_progress(uint64_t elapsed) {
    if (elapsed <= BONGO_CAT_MODEL_LOAD_VISUAL_RAMP_NS)
        return .8f * (float)((double)elapsed /
            BONGO_CAT_MODEL_LOAD_VISUAL_RAMP_NS);
    uint64_t slow_elapsed = elapsed - BONGO_CAT_MODEL_LOAD_VISUAL_RAMP_NS;
    uint64_t slow_duration = BONGO_CAT_MODEL_LOAD_VISUAL_DURATION_NS -
        BONGO_CAT_MODEL_LOAD_VISUAL_RAMP_NS;
    float slow = slow_duration ? (float)((double)slow_elapsed / slow_duration) : 1.0f;
    return .8f + (BONGO_CAT_MODEL_LOAD_VISUAL_WAIT_CAP - .8f) *
        NK_CLAMP(0.0f, slow, 1.0f);
}

void bongo_cat_preferences_model_visual_begin(BongoCatPreferences *value,
    const char *model_id) {
    if (!value || !model_id || !model_id[0]) return;
    value->model_load_visual_active = true;
    value->model_load_visual_started_ns = SDL_GetTicksNS();
    value->model_load_visual_completion_ns = 0;
    value->model_load_progress = 0.0f;
    snprintf(value->model_load_visual_id,
        sizeof(value->model_load_visual_id), "%s", model_id);
}

void bongo_cat_preferences_model_load_progress(BongoCatPreferences *value,
    float progress) {
    if (!value || !value->model_loading) return;
    (void)progress;
    uint64_t now = SDL_GetTicksNS();
    uint64_t elapsed = now - value->model_load_visual_started_ns;
    value->model_load_progress = visual_wait_progress(elapsed);
    value->render_dirty = true;
    bool due = !value->model_load_render_ns ||
        now - value->model_load_render_ns >= MODEL_LOAD_RENDER_INTERVAL_NS;
    if (value->window && due) {
        value->model_load_render_progress = progress;
        value->model_load_render_ns = now;
        bongo_cat_preferences_render(value);
    }
}

float bongo_cat_preferences_model_visual_progress(BongoCatPreferences *value,
    const char *model_id) {
    if (!value || !model_id || !value->model_load_visual_active ||
        strcmp(value->model_load_visual_id, model_id)) return 0.0f;
    uint64_t now = SDL_GetTicksNS();
    uint64_t elapsed = now - value->model_load_visual_started_ns;
    if (!value->model_loading && !value->model_selection_pending) {
        if (!value->model_load_visual_completion_ns)
            value->model_load_visual_completion_ns = now;
        uint64_t completed = now - value->model_load_visual_completion_ns;
        float amount = NK_CLAMP(0.0f, (float)((double)completed /
            BONGO_CAT_MODEL_LOAD_VISUAL_COMPLETE_NS), 1.0f);
        float start = visual_wait_progress(elapsed);
        value->model_load_progress = start + (1.0f - start) * amount;
        if (amount >= 1.0f) {
            value->model_load_visual_active = false;
            value->model_load_visual_completion_ns = 0;
            value->model_load_progress = 1.0f;
        }
        value->render_dirty = true;
        return value->model_load_progress;
    }
    value->model_load_progress = visual_wait_progress(elapsed);
    value->render_dirty = true;
    return value->model_load_progress;
}

static void finish_model_load_progress(BongoCatPreferences *value) {
    if (!value) return;
    if (value->model_load_visual_active) {
        uint64_t now = SDL_GetTicksNS();
        value->model_load_visual_completion_ns = now;
        value->model_load_progress = visual_wait_progress(now -
            value->model_load_visual_started_ns);
    } else value->model_load_progress = 1.0f;
    value->render_dirty = true;
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
    if (!value->model_load_visual_active || strcmp(value->model_load_visual_id, id))
        bongo_cat_preferences_model_visual_begin(value, id);
    snprintf(value->loading_model_id, sizeof(value->loading_model_id), "%s", id);
    BongoCatError error = {0};
    bool selected = bongo_cat_app_select_model_with_error(value->app, id, &error);
    if (selected) finish_model_load_progress(value);
    else {
        value->model_load_progress = 0.0f;
        value->model_load_visual_active = false;
        value->model_load_visual_id[0] = '\0';
    }
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
        if (installed_count && app->preferences->page == 2) {
            app->preferences->font_reload_pending = true;
            app->preferences->font_reload_defer_once = true;
        }
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

static int preset_model_order(const BongoCatModelEntry *entry) {
    if (!entry || !entry->preset) return 3;
    if (!strcmp(entry->id, "standard")) return 0;
    if (!strcmp(entry->id, "keyboard")) return 1;
    if (!strcmp(entry->id, "gamepad")) return 2;
    return 3;
}

static void draw_models(BongoCatPreferences *value,
    struct nk_context *context, bool managed) {
    for (int order = 0; order <= 3; ++order) {
        for (size_t i = 0; i < value->app->models.count; ++i) {
            const BongoCatModelEntry *entry = &value->app->models.entries[i];
            if (entry->managed != managed || preset_model_order(entry) != order)
                continue;
            bongo_cat_preferences_model_card(value, context, entry);
        }
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
