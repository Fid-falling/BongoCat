#include "model_import_mver_internal.h"

#include <stdio.h>
#include <string.h>
#include <yyjson.h>

static int failures;
#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #value); \
    failures++; \
} } while (0)

static bool chord(const char *json, bool gamepad, const char *expected) {
    yyjson_doc *document = yyjson_read(json, strlen(json), 0);
    BongoCatNeoImportCandidate candidate = {0};
    candidate.gamepad_buttons = gamepad;
    char output[BONGO_CAT_NEO_SHORTCUT_CAP];
    bool ok = document && bongo_cat_neo_mver_chord(&candidate,
        yyjson_doc_get_root(document), output, sizeof(output));
    bool matches = ok && expected && strcmp(output, expected) == 0;
    yyjson_doc_free(document);
    return expected ? matches : !ok;
}

int main(void) {
    CHECK(chord("[17,65]", true, "Control+A"));
    CHECK(chord("[0]", true, "Gamepad:South"));
    CHECK(chord("[15]", true, "Gamepad:Select"));
    CHECK(chord("[8]", false, "Backspace"));
    CHECK(chord("[16,65]", false, "Shift+A"));
    CHECK(chord("[17,18,90]", false, "Control+Alt+Z"));
    CHECK(chord("[0]", false, NULL));
    CHECK(chord("[16]", false, NULL));
    CHECK(chord("[17,65,66]", true, NULL));
    BongoCatNeoMverKeyNames modifier = bongo_cat_neo_mver_device_names(16, 1, 2);
    CHECK(modifier.count == 1 && strcmp(modifier.items[0], "ShiftRight") == 0);
    BongoCatNeoMverKeyNames dpad = bongo_cat_neo_mver_gamepad_names(12);
    CHECK(dpad.count == 1 && strcmp(dpad.items[0], "DPadUp") == 0);
    return failures ? 1 : 0;
}
