#include "model_package.h"
#include "bongo_cat/json.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <string.h>
#include <yyjson.h>

static bool safe_relative(const char *value) {
    if (!value || !value[0] || value[0] == '/' || value[0] == '\\' ||
        strchr(value, ':')) return false;
    const char *cursor = value;
    while (*cursor) {
        while (*cursor == '/' || *cursor == '\\') cursor++;
        if (cursor[0] == '.' && cursor[1] == '.' &&
            (!cursor[2] || cursor[2] == '/' || cursor[2] == '\\')) return false;
        cursor = strpbrk(cursor, "/\\");
        if (!cursor) break;
    }
    return true;
}

static bool mode_valid(const char *value) {
    return value && (strcmp(value, "standard") == 0 ||
        strcmp(value, "keyboard") == 0 || strcmp(value, "gamepad") == 0);
}

static BongoCatModelMode stored_mode(const char *value) {
    if (value && strcmp(value, "keyboard") == 0) return BONGO_CAT_MODE_KEYBOARD;
    if (value && strcmp(value, "gamepad") == 0) return BONGO_CAT_MODE_GAMEPAD;
    return BONGO_CAT_MODE_STANDARD;
}

static BongoCatModelSourceFormat stored_format(const char *value) {
    if (value && strcmp(value, "tauri-live2d") == 0)
        return BONGO_CAT_MODEL_SOURCE_TAURI;
    if (value && strcmp(value, "bongo-cat-mver") == 0)
        return BONGO_CAT_MODEL_SOURCE_MVER;
    if (value && strcmp(value, "bongo-cat-mver-patch") == 0)
        return BONGO_CAT_MODEL_SOURCE_MVER_PATCH;
    return BONGO_CAT_MODEL_SOURCE_UNKNOWN;
}

static bool safe_id(const char *value) {
    if (!value || !value[0] || strlen(value) >= BONGO_CAT_ID_CAP ||
        value[0] == '.') return false;
    for (const unsigned char *cursor = (const unsigned char *)value;
        *cursor; ++cursor)
        if (!((*cursor >= 'a' && *cursor <= 'z') ||
            (*cursor >= 'A' && *cursor <= 'Z') ||
            (*cursor >= '0' && *cursor <= '9') || *cursor == '-' ||
            *cursor == '_' || *cursor == '.')) return false;
    return true;
}

static bool digest_valid(const char *value) {
    if (!value || strlen(value) != 64) return false;
    for (const unsigned char *cursor = (const unsigned char *)value;
        *cursor; ++cursor)
        if (!((*cursor >= '0' && *cursor <= '9') ||
            (*cursor >= 'a' && *cursor <= 'f'))) return false;
    return true;
}

static bool text_valid(const char *value) {
    return value && value[0] && strlen(value) < BONGO_CAT_ID_CAP;
}

static bool same_reference(const char *left, const char *right) {
    while (left && right && *left && *right) {
        char a = *left == '\\' ? '/' : *left;
        char b = *right == '\\' ? '/' : *right;
        if (a != b) return false;
        left++; right++;
    }
    return left && right && !*left && !*right;
}

static bool metadata_reference(const char *adapter, const char *metadata) {
    char expected[BONGO_CAT_PATH_CAP];
    return bongo_cat_path_join(expected, sizeof(expected), adapter,
        BONGO_CAT_MODEL_ADAPTER_FILE) && same_reference(expected, metadata);
}

static bool adapter_valid(const char *path, const char *source_format) {
    yyjson_doc *document = bongo_cat_json_read_file(path, 0, NULL);
    yyjson_val *root = document ? yyjson_doc_get_root(document) : NULL;
    const char *kind = yyjson_get_str(yyjson_obj_get(root, "kind"));
    const char *format = yyjson_get_str(yyjson_obj_get(root, "sourceFormat"));
    yyjson_val *render = yyjson_obj_get(root, "render");
    bool valid = yyjson_is_obj(root) &&
        yyjson_get_int(yyjson_obj_get(root, "schemaVersion")) ==
            BONGO_CAT_MODEL_ADAPTER_SCHEMA && kind &&
        strcmp(kind, "bongo-cat-runtime-adapter") == 0 && format &&
        strcmp(format, source_format) == 0 && yyjson_is_obj(render) &&
        text_valid(yyjson_get_str(yyjson_obj_get(render, "profile"))) &&
        yyjson_is_arr(yyjson_obj_get(root, "bindings"));
    yyjson_doc_free(document);
    return valid;
}

typedef struct CapabilityName { const char *name; uint32_t flag; } CapabilityName;

static uint32_t stored_capabilities(yyjson_val *values) {
    static const CapabilityName names[] = {
        {"live2d", BONGO_CAT_MODEL_CAPABILITY_LIVE2D},
        {"preview", BONGO_CAT_MODEL_CAPABILITY_PREVIEW},
        {"runtime-adapter", BONGO_CAT_MODEL_CAPABILITY_RUNTIME_ADAPTER},
        {"input-images", BONGO_CAT_MODEL_CAPABILITY_INPUT_IMAGES},
        {"keyboard-input", BONGO_CAT_MODEL_CAPABILITY_KEYBOARD_INPUT},
        {"gamepad-input", BONGO_CAT_MODEL_CAPABILITY_GAMEPAD_INPUT},
        {"behaviors", BONGO_CAT_MODEL_CAPABILITY_BEHAVIORS},
        {"audio", BONGO_CAT_MODEL_CAPABILITY_AUDIO},
        {"effects", BONGO_CAT_MODEL_CAPABILITY_EFFECTS},
        {"mver-projection", BONGO_CAT_MODEL_CAPABILITY_MVER_PROJECTION},
        {"pointer-overlay", BONGO_CAT_MODEL_CAPABILITY_POINTER_OVERLAY},
        {"image-patch", BONGO_CAT_MODEL_CAPABILITY_IMAGE_PATCH}
    };
    uint32_t result = 0;
    if (!yyjson_is_arr(values)) return result;
    size_t index, count; yyjson_val *item;
    yyjson_arr_foreach(values, index, count, item) {
        const char *value = yyjson_get_str(item);
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            if (value && strcmp(value, names[i].name) == 0) {
                result |= names[i].flag;
                break;
            }
    }
    return result;
}

static bool catalog_has_id(const BongoCatModelCatalog *catalog,
    const char *id) {
    for (size_t i = 0; i < catalog->count; ++i)
        if (strcmp(catalog->entries[i].id, id) == 0) return true;
    return false;
}

bool bongo_cat_model_adapter_metadata_path(const char *directory,
    char *path, size_t capacity) {
    return directory && path && capacity &&
        bongo_cat_path_join(path, capacity, directory,
            BONGO_CAT_MODEL_ADAPTER_FILE);
}

static bool resolve_entry(BongoCatModelEntry *entry, const char *package,
    const char *model, const char *adapter, const char *setting,
    const char *source_format, int schema) {
    char setting_path[BONGO_CAT_PATH_CAP], metadata_path[BONGO_CAT_PATH_CAP];
    memset(entry, 0, sizeof(*entry));
    return bongo_cat_path_join(entry->directory, sizeof(entry->directory),
            package, model) &&
        bongo_cat_path_join(entry->adapter_directory,
            sizeof(entry->adapter_directory), package, adapter) &&
        bongo_cat_path_join(setting_path, sizeof(setting_path), entry->directory,
            setting) && bongo_cat_path_is_file(setting_path) &&
        bongo_cat_path_is_dir(entry->adapter_directory) &&
        schema == BONGO_CAT_MODEL_PACKAGE_SCHEMA &&
        bongo_cat_path_join(metadata_path, sizeof(metadata_path),
            entry->adapter_directory, BONGO_CAT_MODEL_ADAPTER_FILE) &&
        adapter_valid(metadata_path, source_format);
}

bool bongo_cat_model_package_add(BongoCatModelCatalog *catalog,
    const char *directory, bool preset, bool *handled) {
    char descriptor[BONGO_CAT_PATH_CAP];
    *handled = bongo_cat_path_join(descriptor, sizeof(descriptor), directory,
        BONGO_CAT_MODEL_PACKAGE_FILE) && bongo_cat_path_is_file(descriptor);
    if (!*handled) return true;
    yyjson_doc *document = bongo_cat_json_read_file(descriptor, 0, NULL);
    yyjson_val *root = document ? yyjson_doc_get_root(document) : NULL;
    int schema = (int)yyjson_get_int(yyjson_obj_get(root, "schemaVersion"));
    yyjson_val *model_object = yyjson_obj_get(root, "model");
    yyjson_val *runtime_object = yyjson_obj_get(root, "runtime");
    yyjson_val *source_object = yyjson_obj_get(root, "source");
    const char *model = yyjson_get_str(yyjson_obj_get(model_object, "directory"));
    const char *adapter = yyjson_get_str(yyjson_obj_get(runtime_object, "adapter"));
    const char *setting = yyjson_get_str(yyjson_obj_get(model_object, "setting"));
    const char *mode = yyjson_get_str(yyjson_obj_get(root, "mode"));
    const char *source_format = yyjson_get_str(yyjson_obj_get(source_object, "format"));
    const char *source_name = yyjson_get_str(yyjson_obj_get(source_object, "name"));
    const char *source_layout = yyjson_get_str(yyjson_obj_get(source_object, "layout"));
    const char *display = yyjson_get_str(yyjson_obj_get(root, "displayName"));
    const char *package_id = yyjson_get_str(yyjson_obj_get(root, "packageId"));
    const char *digest = yyjson_get_str(yyjson_obj_get(root, "contentDigest"));
    const char *family = yyjson_get_str(yyjson_obj_get(root, "familyId"));
    const char *metadata = yyjson_get_str(yyjson_obj_get(runtime_object, "metadata"));
    int adapter_schema = (int)yyjson_get_int(yyjson_obj_get(runtime_object,
        "adapterSchemaVersion"));
    int adapter_generator = (int)yyjson_get_int(yyjson_obj_get(runtime_object,
        "generatorVersion"));
    uint32_t capabilities = stored_capabilities(
        yyjson_obj_get(root, "capabilities"));
    uint32_t required = BONGO_CAT_MODEL_CAPABILITY_LIVE2D |
        BONGO_CAT_MODEL_CAPABILITY_RUNTIME_ADAPTER;
    bool valid = yyjson_is_obj(root) &&
        schema == BONGO_CAT_MODEL_PACKAGE_SCHEMA &&
        safe_relative(model) && safe_relative(adapter) && safe_relative(setting) &&
        safe_id(package_id) && digest_valid(digest) &&
        (!family || safe_id(family)) &&
        yyjson_is_obj(model_object) && yyjson_is_obj(runtime_object) &&
        yyjson_is_obj(source_object) && safe_id(source_format) &&
        text_valid(source_name) && safe_id(source_layout) &&
        text_valid(display) && mode_valid(mode) &&
        yyjson_is_bool(yyjson_obj_get(source_object, "preserved")) &&
        safe_relative(metadata) && metadata_reference(adapter, metadata) &&
        adapter_schema == BONGO_CAT_MODEL_ADAPTER_SCHEMA &&
        adapter_generator > 0 &&
        yyjson_is_arr(yyjson_obj_get(root, "capabilities")) &&
        (capabilities & required) == required &&
        yyjson_is_obj(yyjson_obj_get(root, "extensions")) &&
        !catalog_has_id(catalog, package_id) &&
        catalog->count < BONGO_CAT_MODEL_CAP;
    BongoCatModelEntry *entry = valid ? &catalog->entries[catalog->count] : NULL;
    if (entry) valid = resolve_entry(entry, directory, model, adapter, setting,
        source_format, schema);
    if (valid) {
        snprintf(entry->id, sizeof(entry->id), "%s", package_id);
        snprintf(entry->package_id, sizeof(entry->package_id), "%s", package_id);
        if (digest) snprintf(entry->content_digest,
            sizeof(entry->content_digest), "%s", digest);
        if (family) snprintf(entry->family_id, sizeof(entry->family_id), "%s", family);
        if (display) snprintf(entry->display_name,
            sizeof(entry->display_name), "%s", display);
        snprintf(entry->storage_directory, sizeof(entry->storage_directory),
            "%s", directory);
        snprintf(entry->setting_file, sizeof(entry->setting_file), "%s", setting);
        entry->mode = stored_mode(mode);
        entry->source_format = stored_format(source_format);
        entry->capabilities = capabilities;
        entry->package_schema = schema;
        entry->adapter_schema = adapter_schema;
        entry->adapter_generator = adapter_generator;
        entry->preset = preset;
        catalog->count++;
    }
    yyjson_doc_free(document);
    return valid;
}
