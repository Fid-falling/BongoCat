#include "model_import_portable_internal.h"
#include "runtime.h"
#include "bongo_cat/json.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

#define PORTABLE_CACHE_CAP 256

typedef struct PortableCacheEntry {
    char id[BONGO_CAT_ID_CAP];
    char source[BONGO_CAT_PATH_CAP];
    char identity[65];
    BongoCatModelMode model_mode;
    bool active;
} PortableCacheEntry;

typedef struct PortableCacheList {
    BongoCatApp *app;
    PortableCacheEntry entries[PORTABLE_CACHE_CAP];
    size_t count;
    bool overflow;
} PortableCacheList;

static bool parse_mode(const char *value, BongoCatModelMode *model_mode) {
    for (int i = 0; i <= BONGO_CAT_MODE_GAMEPAD; ++i)
        if (value && strcmp(value, bongo_cat_mode_name((BongoCatModelMode)i)) == 0) {
            *model_mode = (BongoCatModelMode)i; return true;
        }
    return false;
}

static BongoCatPathVisit collect_cache(void *userdata,
    const char *dirname, const char *name) {
    PortableCacheList *list = userdata;
    char directory[BONGO_CAT_PATH_CAP], marker[BONGO_CAT_PATH_CAP];
    if (name[0] == '.' || !bongo_cat_path_join(directory, sizeof(directory),
        dirname, name) || !bongo_cat_path_is_dir(directory) ||
        !bongo_cat_path_join(marker, sizeof(marker), directory,
        BONGO_CAT_PORTABLE_MARKER)) return BONGO_CAT_PATH_CONTINUE;
    yyjson_doc *document = bongo_cat_json_read_file(marker, 0, NULL);
    yyjson_val *root = document ? yyjson_doc_get_root(document) : NULL;
    const char *kind = yyjson_get_str(yyjson_obj_get(root, "kind"));
    const char *source = yyjson_get_str(yyjson_obj_get(root, "source"));
    const char *identity = yyjson_get_str(yyjson_obj_get(root, "identity"));
    const char *mode_name = yyjson_get_str(yyjson_obj_get(root, "mode"));
    BongoCatModelMode model_mode = BONGO_CAT_MODE_STANDARD;
    bool valid = yyjson_is_obj(root) && kind && source && identity &&
        strlen(identity) == 64 && strcmp(kind, "portable-mver") == 0 &&
        parse_mode(mode_name, &model_mode);
    if (valid && list->count >= PORTABLE_CACHE_CAP) list->overflow = true;
    if (valid && !list->overflow) {
        PortableCacheEntry *entry = &list->entries[list->count++];
        snprintf(entry->id, sizeof(entry->id), "%s", name);
        snprintf(entry->source, sizeof(entry->source), "%s", source);
        snprintf(entry->identity, sizeof(entry->identity), "%s", identity);
        entry->model_mode = model_mode;
        entry->active = bongo_cat_models_find(&list->app->models, entry->id) != NULL;
    }
    yyjson_doc_free(document);
    return list->overflow ? BONGO_CAT_PATH_SUCCESS : BONGO_CAT_PATH_CONTINUE;
}

static bool shortcut_prefix(const char *id, const char *model_id) {
    size_t length = strlen(model_id);
    return strncmp(id, model_id, length) == 0 && id[length] == ':';
}

static bool config_references(const BongoCatConfig *config, const char *id) {
    if (strcmp(config->current_model, id) == 0 ||
        bongo_cat_config_model_label(config, id)) return true;
    for (size_t i = 0; i < config->behavior_shortcut_count; ++i)
        if (shortcut_prefix(config->behavior_shortcuts[i].id, id)) return true;
    return false;
}

static void migrate_label(BongoCatConfig *config, const char *old_id,
    const char *new_id) {
    const char *new_label = bongo_cat_config_model_label(config, new_id);
    for (size_t i = 0; i < config->model_label_count; ++i) {
        BongoCatModelLabel *label = &config->model_labels[i];
        if (strcmp(label->id, old_id) != 0) continue;
        if (!new_label) snprintf(label->id, sizeof(label->id), "%s", new_id);
        else bongo_cat_config_set_model_label(config, old_id, "");
        return;
    }
}

static size_t shortcut_at(const BongoCatConfig *config, const char *id) {
    for (size_t i = 0; i < config->behavior_shortcut_count; ++i)
        if (strcmp(config->behavior_shortcuts[i].id, id) == 0) return i;
    return config->behavior_shortcut_count;
}

static void remove_shortcut(BongoCatConfig *config, size_t index) {
    config->behavior_shortcut_count--;
    if (index < config->behavior_shortcut_count)
        memmove(&config->behavior_shortcuts[index],
            &config->behavior_shortcuts[index + 1],
            (config->behavior_shortcut_count - index) *
            sizeof(config->behavior_shortcuts[0]));
    memset(&config->behavior_shortcuts[config->behavior_shortcut_count], 0,
        sizeof(config->behavior_shortcuts[0]));
}

static void migrate_shortcuts(BongoCatConfig *config, const char *old_id,
    const char *new_id) {
    size_t old_length = strlen(old_id);
    for (size_t i = 0; i < config->behavior_shortcut_count;) {
        BongoCatBehaviorShortcut *old = &config->behavior_shortcuts[i];
        if (!shortcut_prefix(old->id, old_id)) { i++; continue; }
        char id[BONGO_CAT_BEHAVIOR_ID_CAP];
        snprintf(id, sizeof(id), "%s%s", new_id, old->id + old_length);
        size_t target = shortcut_at(config, id);
        if (target == config->behavior_shortcut_count) {
            snprintf(old->id, sizeof(old->id), "%s", id); i++; continue;
        }
        BongoCatBehaviorShortcut *existing = &config->behavior_shortcuts[target];
        if (!existing->shortcut[0]) snprintf(existing->shortcut,
            sizeof(existing->shortcut), "%s", old->shortcut);
        if (!existing->label[0]) snprintf(existing->label,
            sizeof(existing->label), "%s", old->label);
        remove_shortcut(config, i);
    }
}

static void migrate(BongoCatApp *app, const char *old_id,
    const char *new_id) {
    migrate_label(&app->config, old_id, new_id);
    migrate_shortcuts(&app->config, old_id, new_id);
    if (strcmp(app->config.current_model, old_id) == 0)
        snprintf(app->config.current_model, sizeof(app->config.current_model),
            "%s", new_id);
    SDL_Log("Migrated portable model preferences from %s to %s", old_id, new_id);
}

void bongo_cat_portable_migrate_config(BongoCatApp *app,
    const char *cache_root) {
    PortableCacheList *list = calloc(1, sizeof(*list));
    if (!app || !cache_root || !list) { free(list); return; }
    list->app = app;
    if (!bongo_cat_path_enumerate(cache_root, collect_cache, list) ||
        list->overflow) { free(list); return; }
    for (size_t i = 0; i < list->count; ++i) {
        PortableCacheEntry *old = &list->entries[i];
        if (old->active || bongo_cat_path_is_dir(old->source) ||
            !config_references(&app->config, old->id)) continue;
        PortableCacheEntry *replacement = NULL;
        size_t candidates = 0, active = 0;
        for (size_t j = 0; j < list->count; ++j) {
            PortableCacheEntry *item = &list->entries[j];
            if (item == old || item->model_mode != old->model_mode ||
                strcmp(item->identity, old->identity) != 0 ||
                (!item->active && !bongo_cat_path_is_dir(item->source))) continue;
            candidates++;
            if (item->active) { replacement = item; active++; }
        }
        if (candidates == 1 && active == 1)
            migrate(app, old->id, replacement->id);
    }
    free(list);
}
