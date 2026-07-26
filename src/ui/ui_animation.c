#include "ui_animation.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct AnimationSlot {
    struct nk_context *context;
    char id[80];
    float current;
    float start;
    float target;
    float duration_ms;
    uint64_t start_ns;
    uint64_t last_ns;
    bool used;
} AnimationSlot;

static AnimationSlot slots[96];

static AnimationSlot *slot_for(struct nk_context *context, const char *id,
    float target) {
    AnimationSlot *empty = NULL;
    for (size_t i = 0; i < sizeof(slots) / sizeof(slots[0]); ++i) {
        AnimationSlot *slot = &slots[i];
        if (slot->used && slot->context == context && !strcmp(slot->id, id))
            return slot;
        if (!slot->used && !empty) empty = slot;
    }
    if (!empty) empty = &slots[0];
    memset(empty, 0, sizeof(*empty));
    empty->used = true; empty->context = context;
    empty->current = empty->start = empty->target = target;
    empty->start_ns = empty->last_ns = SDL_GetTicksNS();
    snprintf(empty->id, sizeof(empty->id), "%s", id);
    return empty;
}

float bongo_cat_neo_ui_animate(struct nk_context *context, const char *id,
    float target, float duration_ms) {
    AnimationSlot *slot = slot_for(context, id, target);
    uint64_t now = SDL_GetTicksNS();
    if (slot->target != target) {
        slot->start = slot->current;
        slot->target = target;
        slot->duration_ms = duration_ms;
        slot->start_ns = now;
    }
    slot->last_ns = now;
    if (slot->current == target) return target;
    float elapsed_ms = (float)(now - slot->start_ns) / 1000000.0f;
    float progress = slot->duration_ms > 0 ? elapsed_ms / slot->duration_ms : 1.0f;
    progress = NK_CLAMP(0.0f, progress, 1.0f);
    float remaining = 1.0f - progress;
    float eased = 1.0f - remaining * remaining * remaining;
    slot->current = slot->start + (slot->target - slot->start) * eased;
    if (progress >= 1.0f || fabsf(slot->current - target) < .001f)
        slot->current = target;
    return slot->current;
}

bool bongo_cat_neo_ui_animations_active(const struct nk_context *context) {
    for (size_t i = 0; i < sizeof(slots) / sizeof(slots[0]); ++i)
        if (slots[i].used && slots[i].context == context &&
            fabsf(slots[i].current - slots[i].target) >= .001f) return true;
    return false;
}

void bongo_cat_neo_ui_animations_reset(const struct nk_context *context) {
    for (size_t i = 0; i < sizeof(slots) / sizeof(slots[0]); ++i)
        if (slots[i].used && slots[i].context == context)
            memset(&slots[i], 0, sizeof(slots[i]));
}
