#include "bongo_cat/config.h"
#include "bongo_cat/utf8.h"

#include <math.h>
#include <string.h>

static float clampf_or(float value, float low, float high, float fallback) {
    if (!isfinite(value)) return fallback;
    return value < low ? low : value > high ? high : value;
}

static bool normalize_text(char *text, size_t capacity) {
    text[capacity - 1] = '\0';
    if (!bongo_cat_utf8_valid(text)) {
        memset(text, 0, capacity);
        return false;
    }
    size_t length = strlen(text);
    memset(text + length + 1, 0, capacity - length - 1);
    return true;
}

void bongo_cat_session_defaults(BongoCatSessionState *session) {
    if (!session) return;
    memset(session, 0, sizeof(*session));
    session->window.visible = true;
    session->window.scale_percent = BONGO_CAT_DEFAULT_WINDOW_SCALE_PERCENT;
    session->window.opacity_percent =
        BONGO_CAT_DEFAULT_WINDOW_OPACITY_PERCENT;
    session->window.width = 612;
    session->window.height = 354;
    memcpy(session->active_model_id, "standard", sizeof("standard"));
}

static void compact_active_behaviors(BongoCatSessionState *session) {
    size_t count = session->active_behavior_count;
    if (count > BONGO_CAT_BEHAVIOR_BINDING_CAP)
        count = BONGO_CAT_BEHAVIOR_BINDING_CAP;
    size_t output = 0;
    for (size_t i = 0; i < count; ++i) {
        BongoCatActiveBehavior entry = session->active_behaviors[i];
        if (!normalize_text(entry.model_id, sizeof(entry.model_id)) ||
            !normalize_text(entry.behavior_id, sizeof(entry.behavior_id)) ||
            !entry.model_id[0] || !entry.behavior_id[0]) continue;
        bool duplicate = false;
        for (size_t j = 0; j < output; ++j)
            if (!strcmp(session->active_behaviors[j].model_id,
                    entry.model_id) &&
                !strcmp(session->active_behaviors[j].behavior_id,
                    entry.behavior_id)) {
                duplicate = true;
                break;
            }
        if (!duplicate) session->active_behaviors[output++] = entry;
    }
    if (output < BONGO_CAT_BEHAVIOR_BINDING_CAP)
        memset(&session->active_behaviors[output], 0,
            (BONGO_CAT_BEHAVIOR_BINDING_CAP - output) *
            sizeof(session->active_behaviors[0]));
    session->active_behavior_count = output;
}

void bongo_cat_session_validate(BongoCatSessionState *session) {
    if (!session) return;
    session->window.scale_percent = clampf_or(session->window.scale_percent,
        10.0f, 500.0f, BONGO_CAT_DEFAULT_WINDOW_SCALE_PERCENT);
    session->window.opacity_percent = clampf_or(
        session->window.opacity_percent, 10.0f, 100.0f,
        BONGO_CAT_DEFAULT_WINDOW_OPACITY_PERCENT);
    if (session->window.width < 64) session->window.width = 64;
    if (session->window.height < 64) session->window.height = 64;
    if (session->window.width > 8192) session->window.width = 8192;
    if (session->window.height > 8192) session->window.height = 8192;
    if (!normalize_text(session->active_model_id,
            sizeof(session->active_model_id)) ||
        !session->active_model_id[0])
        memcpy(session->active_model_id, "standard", sizeof("standard"));
    compact_active_behaviors(session);
}
