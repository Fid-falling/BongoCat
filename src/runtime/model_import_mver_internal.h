#ifndef BONGO_CAT_NEO_MODEL_IMPORT_MVER_INTERNAL_H
#define BONGO_CAT_NEO_MODEL_IMPORT_MVER_INTERNAL_H

#include "model_import.h"

typedef struct BongoCatNeoMverKeyNames {
    const char *items[2];
    char generated[16];
    size_t count;
} BongoCatNeoMverKeyNames;

bool bongo_cat_neo_mver_emit_pair(const char *hand, const char *keyboard,
    const char *directory, BongoCatNeoMverKeyNames names, BongoCatNeoError *error);
BongoCatNeoMverKeyNames bongo_cat_neo_mver_device_names(int code,
    size_t occurrence, size_t total);
BongoCatNeoMverKeyNames bongo_cat_neo_mver_gamepad_names(int code);
int bongo_cat_neo_mver_modifier_index(int code);
bool bongo_cat_neo_mver_chord(const BongoCatNeoImportCandidate *candidate,
    void *row, char *output, size_t capacity);
bool bongo_cat_neo_mver_effects(void *output, void *items, void *root, void *mode,
    const BongoCatNeoImportCandidate *candidate, const char *target);
bool bongo_cat_neo_mver_add_behaviors(void *output, void *items, void *mode,
    const BongoCatNeoImportCandidate *candidate, BongoCatNeoError *error);

#endif
