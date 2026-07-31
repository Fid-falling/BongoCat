#include "runtime.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct PointerAudit {
    bool ran;
    float start_angle;
    float first_angle;
    float final_angle;
    float maximum_step;
    float start_mouse;
    float final_mouse;
} PointerAudit;

static PointerAudit pointer_audit;

/* TargetPoint is capped at 4 degrees per 60 Hz step. The authored breath
   track adds a small change on the same parameter after pointer motion. */
#define POINTER_VISIBLE_STEP_LIMIT 4.1f

static bool value(BongoCatApp *app, const char *id, float *output) {
    BongoCatParameterRange range;
    if (!bongo_cat_live2d_parameter(app->live2d, id, &range)) return false;
    if (output) *output = range.value;
    return true;
}

static void input(BongoCatApp *app, BongoCatInputKind kind, const char *name, float value) {
    BongoCatInputEvent event = {.kind = kind, .value = value};
    snprintf(event.name, sizeof(event.name), "%s", name);
    bongo_cat_app_shortcuts(app, &event);
    bongo_cat_app_apply_input(app, &event);
}

static bool motion(BongoCatApp *app, const char *scenario) {
    if (strcmp(scenario, "motion-0") == 0)
        return bongo_cat_live2d_start_motion(app->live2d, "CAT_motion", 0);
    if (strcmp(scenario, "motion-1") == 0)
        return bongo_cat_live2d_start_motion(app->live2d, "CAT_motion", 1);
    if (strncmp(scenario, "expression-", 11) == 0)
        return bongo_cat_live2d_set_expression(app->live2d, atoi(scenario + 11));
    return true;
}

static bool pointer(BongoCatApp *app, bool mirror) {
    SDL_Rect bounds;
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    if (!display || !SDL_GetDisplayBounds(display, &bounds)) return false;
    app->config.model.mouse_mirror = mirror;
    bongo_cat_app_apply_mouse_position(app, bounds.x + bounds.w * 0.9,
        bounds.y + bounds.h * 0.1, 1.0f / 60.0f);
    // Dragging is intentionally smoothed by Cubism's TargetPoint. Advance
    // enough frames to observe the settled direction, as a real render loop
    // does, rather than asserting on its first acceleration step.
    for (int frame = 0; frame < 90; ++frame)
        bongo_cat_app_step_live2d(app, 1.0f / 60.0f);
    return true;
}

static bool reverse_pointer(BongoCatApp *app) {
    SDL_Rect bounds;
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    if (!display || !SDL_GetDisplayBounds(display, &bounds)) return false;
    pointer_audit = (PointerAudit){.ran = true};
    app->config.model.mouse_mirror = false;
    double y = bounds.y + bounds.h * 0.5;
    bongo_cat_app_apply_mouse_position(app, bounds.x + bounds.w * 0.1, y,
        1.0f / 60.0f);
    for (int frame = 0; frame < 90; ++frame)
        bongo_cat_app_step_live2d(app, 1.0f / 60.0f);
    if (!value(app, "ParamAngleX", &pointer_audit.start_angle) ||
        !value(app, "ParamMouseX", &pointer_audit.start_mouse)) return false;
    bongo_cat_app_apply_mouse_position(app, bounds.x + bounds.w * 0.9, y,
        1.0f / 60.0f);
    float previous = pointer_audit.start_angle;
    for (int frame = 0; frame < 90; ++frame) {
        bongo_cat_app_step_live2d(app, 1.0f / 60.0f);
        float current = previous;
        if (!value(app, "ParamAngleX", &current)) return false;
        if (!frame) pointer_audit.first_angle = current;
        float step = SDL_fabsf(current - previous);
        if (step > pointer_audit.maximum_step)
            pointer_audit.maximum_step = step;
        previous = current;
    }
    pointer_audit.final_angle = previous;
    return value(app, "ParamMouseX", &pointer_audit.final_mouse);
}

static bool apply(BongoCatApp *app, const char *scenario) {
    if (strncmp(scenario, "switch:", 7) == 0)
        return bongo_cat_app_select_model(app, scenario + 7);
    if (strcmp(scenario, "mirror") == 0) app->config.model.mirror = true;
    else if (strcmp(scenario, "mouse-move") == 0) return pointer(app, false);
    else if (strcmp(scenario, "mouse-move-mirror") == 0) return pointer(app, true);
    else if (strcmp(scenario, "mouse-reverse") == 0)
        return reverse_pointer(app);
    else if (strcmp(scenario, "key-left") == 0)
        input(app, BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 1.0f);
    else if (strcmp(scenario, "key-tab-left") == 0)
        input(app, BONGO_CAT_INPUT_KEY_DOWN, "Tab", 1.0f);
    else if (strcmp(scenario, "key-right") == 0)
        input(app, BONGO_CAT_INPUT_KEY_DOWN, "RightArrow", 1.0f);
    else if (strcmp(scenario, "key-left-release") == 0) {
        input(app, BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 1.0f);
        input(app, BONGO_CAT_INPUT_KEY_UP, "KeyA", 0.0f);
    } else if (strcmp(scenario, "keys-both-release") == 0) {
        input(app, BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 1.0f);
        input(app, BONGO_CAT_INPUT_KEY_DOWN, "RightArrow", 1.0f);
        input(app, BONGO_CAT_INPUT_KEY_UP, "KeyA", 0.0f);
        input(app, BONGO_CAT_INPUT_KEY_UP, "RightArrow", 0.0f);
    } else if (strcmp(scenario, "key-stress") == 0) {
        for (int i = 0; i < 250; ++i) {
            input(app, BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 1.0f);
            input(app, BONGO_CAT_INPUT_KEY_DOWN, "RightArrow", 1.0f);
            input(app, BONGO_CAT_INPUT_KEY_UP, "KeyA", 0.0f);
            input(app, BONGO_CAT_INPUT_KEY_UP, "RightArrow", 0.0f);
        }
    }
    else if (strcmp(scenario, "keys-both") == 0) {
        input(app, BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 1.0f);
        input(app, BONGO_CAT_INPUT_KEY_DOWN, "RightArrow", 1.0f);
    } else if (strcmp(scenario, "mouse-left") == 0)
        input(app, BONGO_CAT_INPUT_MOUSE_DOWN, "Left", 1.0f);
    else if (strcmp(scenario, "mouse-right") == 0)
        input(app, BONGO_CAT_INPUT_MOUSE_DOWN, "Right", 1.0f);
    else if (strcmp(scenario, "gamepad-buttons") == 0) {
        input(app, BONGO_CAT_INPUT_GAMEPAD_BUTTON, "DPadLeft", 1.0f);
        input(app, BONGO_CAT_INPUT_GAMEPAD_BUTTON, "South", 1.0f);
    } else if (strcmp(scenario, "gamepad-sticks") == 0) {
        input(app, BONGO_CAT_INPUT_GAMEPAD_AXIS, "LeftStickX", .75f);
        input(app, BONGO_CAT_INPUT_GAMEPAD_AXIS, "LeftStickY", -.5f);
        input(app, BONGO_CAT_INPUT_GAMEPAD_AXIS, "RightStickX", -.65f);
        input(app, BONGO_CAT_INPUT_GAMEPAD_AXIS, "RightStickY", .5f);
        input(app, BONGO_CAT_INPUT_GAMEPAD_BUTTON, "LeftThumb", 1.0f);
        input(app, BONGO_CAT_INPUT_GAMEPAD_BUTTON, "RightThumb", 1.0f);
    } else return motion(app, scenario);
    app->dirty = true;
    return true;
}

static void parameter(FILE *file, BongoCatApp *app, const char *id) {
    BongoCatParameterRange range;
    if (bongo_cat_live2d_parameter(app->live2d, id, &range))
        fprintf(file, "parameter.%s=%.4f [%.4f,%.4f]\n", id,
            range.value, range.minimum, range.maximum);
    else fprintf(file, "parameter.%s=unavailable\n", id);
}

static bool active(BongoCatApp *app, const char *id) {
    float current = 0.0f;
    return value(app, id, &current) && current > 0.5f;
}

static bool signed_value(BongoCatApp *app, const char *id, bool positive) {
    float current = 0.0f;
    return value(app, id, &current) && (positive ? current > 0.05f : current < -0.05f);
}

static bool assertions(BongoCatApp *app, const char *scenario, bool operation) {
    if (!operation) return false;
    if (strncmp(scenario, "switch:", 7) == 0)
        return strcmp(app->config.current_model, scenario + 7) == 0;
    if (strcmp(scenario, "idle") == 0 || strcmp(scenario, "mirror") == 0 ||
        strcmp(scenario, "key-left-release") == 0 ||
        strcmp(scenario, "keys-both-release") == 0 ||
        strcmp(scenario, "key-stress") == 0 ||
        strncmp(scenario, "motion-", 7) == 0 ||
        strncmp(scenario, "expression-", 11) == 0) return true;
    if (strcmp(scenario, "key-left") == 0 || strcmp(scenario, "key-tab-left") == 0)
        return active(app, "CatParamLeftHandDown");
    if (strcmp(scenario, "key-right") == 0)
        return active(app, "CatParamRightHandDown");
    if (strcmp(scenario, "keys-both") == 0 ||
        strcmp(scenario, "gamepad-buttons") == 0)
        return active(app, "CatParamLeftHandDown") &&
            active(app, "CatParamRightHandDown");
    if (strcmp(scenario, "mouse-left") == 0)
        return active(app, "ParamMouseLeftDown");
    if (strcmp(scenario, "mouse-right") == 0)
        return active(app, "ParamMouseRightDown");
    if (strcmp(scenario, "mouse-move") == 0)
        return signed_value(app, "ParamAngleX", false) &&
            signed_value(app, "ParamAngleY", true);
    if (strcmp(scenario, "mouse-move-mirror") == 0)
        return signed_value(app, "ParamAngleX", true) &&
            signed_value(app, "ParamAngleY", true);
    if (strcmp(scenario, "mouse-reverse") == 0)
        return pointer_audit.ran && pointer_audit.start_angle > 5.0f &&
            pointer_audit.final_angle < -5.0f &&
            pointer_audit.start_mouse > 20.0f &&
            pointer_audit.final_mouse < -20.0f &&
            SDL_fabsf(pointer_audit.first_angle - pointer_audit.start_angle) <
                POINTER_VISIBLE_STEP_LIMIT &&
            pointer_audit.maximum_step < POINTER_VISIBLE_STEP_LIMIT;
    if (strcmp(scenario, "gamepad-sticks") == 0)
        return signed_value(app, "CatParamStickLX", true) &&
            signed_value(app, "CatParamStickLY", false) &&
            signed_value(app, "CatParamStickRX", false) &&
            signed_value(app, "CatParamStickRY", true) &&
            active(app, "CatParamLeftHandDown") &&
            active(app, "CatParamRightHandDown");
    return false;
}

void bongo_cat_live2d_audit_run(BongoCatApp *app) {
    if (!app || !app->smoke_live2d_scenario[0]) return;
    uint64_t started = SDL_GetTicksNS();
    bool result = apply(app, app->smoke_live2d_scenario);
    double duration_ms = (SDL_GetTicksNS() - started) / 1000000.0;
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), app->data_root, "live2d-audit.txt")) return;
    FILE *file = bongo_cat_file_open(path, "wb");
    if (!file) return;
    bool verified = assertions(app, app->smoke_live2d_scenario, result);
    fprintf(file, "scenario=%s\nmodel=%s\nmode=%s\noperation=%s\nassertions=%s\n"
        "duration_ms=%.3f\n",
        app->smoke_live2d_scenario, app->config.current_model,
        bongo_cat_mode_name(app->config.current_mode), result ? "accepted" : "rejected",
        verified ? "passed" : "failed", duration_ms);
#ifdef BONGO_CAT_HAS_CUBISM
    fputs("renderer=cubism-native\n", file);
    if (!verified) app->exit_code = 1;
#else
    fputs("renderer=diagnostic; visual-model-result=blocked\n", file);
#endif
    const char *parameters[] = {"CatParamLeftHandDown", "CatParamRightHandDown",
        "CatParamStickLX", "CatParamStickLY", "CatParamStickRX", "CatParamStickRY",
        "CatParamStickLeftDown", "CatParamStickRightDown",
        "CatParamStickShowLeftHand", "CatParamStickShowRightHand",
        "ParamMouseLeftDown", "ParamMouseRightDown", "ParamMouseX", "ParamMouseY",
        "ParamAngleX", "ParamAngleY", "ParamAngleZ", "ParamBodyAngleX",
        "ParamEyeBallX", "ParamEyeBallY"};
    for (size_t i = 0; i < sizeof(parameters) / sizeof(parameters[0]); ++i)
        parameter(file, app, parameters[i]);
    if (pointer_audit.ran) fprintf(file,
        "pointer.start_angle=%.4f\npointer.first_angle=%.4f\n"
        "pointer.final_angle=%.4f\npointer.maximum_step=%.4f\n"
        "pointer.start_mouse=%.4f\npointer.final_mouse=%.4f\n",
        pointer_audit.start_angle, pointer_audit.first_angle,
        pointer_audit.final_angle, pointer_audit.maximum_step,
        pointer_audit.start_mouse, pointer_audit.final_mouse);
    fclose(file);
}
