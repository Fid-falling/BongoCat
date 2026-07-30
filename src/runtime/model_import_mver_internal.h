#ifndef BONGO_CAT_MODEL_IMPORT_MVER_INTERNAL_H
#define BONGO_CAT_MODEL_IMPORT_MVER_INTERNAL_H

#include "model_import.h"

typedef struct BongoCatMverKeyNames {
    const char *items[2];
    char generated[16];
    size_t count;
} BongoCatMverKeyNames;

bool bongo_cat_mver_emit_pair(const char *hand, const char *keyboard,
    const char *directory, BongoCatMverKeyNames names, BongoCatError *error);
BongoCatMverKeyNames bongo_cat_mver_device_names(int code,
    size_t occurrence, size_t total);
BongoCatMverKeyNames bongo_cat_mver_gamepad_names(int code);
int bongo_cat_mver_modifier_index(int code);
bool bongo_cat_mver_chord(const BongoCatImportCandidate *candidate,
    void *row, char *output, size_t capacity);
bool bongo_cat_mver_effects(void *output, void *items, void *root, void *mode,
    const BongoCatImportCandidate *candidate, const char *target);
bool bongo_cat_mver_add_behaviors(void *output, void *items, void *mode,
    const BongoCatImportCandidate *candidate, BongoCatError *error);

#endif
