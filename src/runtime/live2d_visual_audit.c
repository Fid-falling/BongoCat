#include "runtime.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"

#include <math.h>
#include <SDL3/SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>

static void advance(BongoCatApp *app, int frames) {
    for (int i = 0; i < frames; ++i)
        bongo_cat_live2d_update(app->live2d, 1.0f / 60.0f);
}

static bool close_scale(float left, float right) {
    return fabsf(left - right) <= 0.0001f;
}

static unsigned edge_pixels(int width, int height) {
    size_t bytes = (size_t)(width > height ? width : height) * 4;
    unsigned char *pixels = malloc(bytes);
    if (!pixels) return ~0u;
    unsigned visible = 0;
    const int rectangles[][4] = {{0, 0, width, 1}, {0, height - 1, width, 1},
        {0, 0, 1, height}, {width - 1, 0, 1, height}};
    for (size_t side = 0; side < 4; ++side) {
        int count = rectangles[side][2] * rectangles[side][3];
        glReadPixels(rectangles[side][0], rectangles[side][1],
            rectangles[side][2], rectangles[side][3], GL_RGBA,
            GL_UNSIGNED_BYTE, pixels);
        for (int i = 0; i < count; ++i)
            if (pixels[(size_t)i * 4 + 3] > 8) visible++;
    }
    free(pixels);
    return visible;
}

static bool record(FILE *file, BongoCatApp *app, const char *name,
    bool contained, BongoCatLive2DVisualState *state) {
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(app->window, &width, &height);
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f); glClear(GL_COLOR_BUFFER_BIT);
    bongo_cat_live2d_draw(app->live2d);
    unsigned edges = edge_pixels(width, height);
    bool available = bongo_cat_live2d_visual_state(app->live2d, state);
    bool finite = available && isfinite(state->fit_scale) &&
        isfinite(state->fit_translate_x) && isfinite(state->fit_translate_y) &&
        isfinite(state->visible_min_x) && isfinite(state->visible_min_y) &&
        isfinite(state->visible_max_x) && isfinite(state->visible_max_y);
    bool visible = finite && state->visible &&
        state->visible_min_x < state->visible_max_x &&
        state->visible_min_y < state->visible_max_y;
    bool inside = !contained || (visible && state->visible_min_x >= -1.08f &&
        state->visible_min_y >= -1.08f && state->visible_max_x <= 1.08f &&
        state->visible_max_y <= 1.08f);
    bool passed = visible && state->fit_scale > 0.0f &&
        state->fit_scale <= 1.0001f && inside;
    fprintf(file, "%s,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%d,%u,%d\n",
        name, state->fit_scale, state->fit_translate_x,
        state->fit_translate_y, state->visible_min_x, state->visible_min_y,
        state->visible_max_x, state->visible_max_y, state->fitted,
        state->visible, edges, passed);
    return passed;
}

static bool expression_matrix(FILE *file, BongoCatApp *app, int expression,
    int width, int height) {
    bool passed = true;
    BongoCatLive2DVisualState enter = {0}, stable = {0}, current = {0};
    bongo_cat_live2d_set_dragging(app->live2d, 0.0f, 0.0f);
    advance(app, 90);
    if (!bongo_cat_live2d_set_expression(app->live2d, expression)) return false;
    advance(app, 1);
    char label[48];
    snprintf(label, sizeof(label), "expression-%d-enter", expression);
    passed = record(file, app, label, false, &enter) && passed;
    advance(app, 120);
    snprintf(label, sizeof(label), "expression-%d-stable", expression);
    passed = record(file, app, label, true, &stable) && passed;
    passed = close_scale(enter.fit_scale, stable.fit_scale) && passed;
    if (expression == 0)
        passed = !stable.fitted && close_scale(stable.fit_scale, 1.0f) && passed;
    if (expression == 1) passed = stable.fit_scale >= 0.85f && passed;
    if (expression == 2) passed = stable.fit_scale >= 0.70f && passed;

    const float pointers[][2] = {{-1.0f, 1.0f}, {1.0f, -1.0f}};
    for (int i = 0; i < 2; ++i) {
        bongo_cat_live2d_set_dragging(app->live2d, pointers[i][0], pointers[i][1]);
        advance(app, 90);
        snprintf(label, sizeof(label), "expression-%d-pointer-%d", expression, i);
        passed = record(file, app, label, true, &current) &&
            close_scale(stable.fit_scale, current.fit_scale) && passed;
    }
    bongo_cat_live2d_set_mirror(app->live2d, true);
    snprintf(label, sizeof(label), "expression-%d-mirror", expression);
    passed = record(file, app, label, true, &current) &&
        close_scale(stable.fit_scale, current.fit_scale) && passed;
    bongo_cat_live2d_reshape(app->live2d, width / 2, height / 2);
    snprintf(label, sizeof(label), "expression-%d-scale-50", expression);
    passed = record(file, app, label, true, &current) &&
        close_scale(stable.fit_scale, current.fit_scale) && passed;
    bongo_cat_live2d_reshape(app->live2d, width * 2, height * 2);
    snprintf(label, sizeof(label), "expression-%d-scale-200", expression);
    passed = record(file, app, label, true, &current) &&
        close_scale(stable.fit_scale, current.fit_scale) && passed;
    bongo_cat_live2d_reshape(app->live2d, width, height);
    bongo_cat_live2d_set_mirror(app->live2d, false);
    return passed;
}

bool bongo_cat_live2d_visual_audit_run(BongoCatApp *app) {
    if (!app || !app->live2d || !app->window) return false;
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), app->data_root,
        "live2d-visual-audit.csv")) return false;
    FILE *file = bongo_cat_file_open(path, "wb");
    if (!file) return false;
    fputs("case,fit_scale,translate_x,translate_y,min_x,min_y,max_x,max_y,"
        "fitted,visible,edge_pixels,passed\n", file);
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(app->window, &width, &height);
    bool passed = width > 1 && height > 1;
    BongoCatLive2DVisualState baseline = {0}, current = {0};
    bongo_cat_live2d_set_expression(app->live2d, -1);
    bongo_cat_live2d_set_dragging(app->live2d, 0.0f, 0.0f);
    advance(app, 90);
    passed = record(file, app, "idle", true, &baseline) && passed;
    passed = !baseline.fitted && close_scale(baseline.fit_scale, 1.0f) && passed;
    const float pointers[][2] = {{-1.0f, -1.0f}, {1.0f, 1.0f}};
    for (int i = 0; i < 2; ++i) {
        bongo_cat_live2d_set_dragging(app->live2d, pointers[i][0], pointers[i][1]);
        advance(app, 90);
        char label[32]; snprintf(label, sizeof(label), "pointer-%d", i);
        passed = record(file, app, label, true, &current) &&
            close_scale(baseline.fit_scale, current.fit_scale) &&
            !current.fitted && passed;
    }
    for (int expression = 0; expression < 3; ++expression) {
        passed = expression_matrix(file, app, expression, width, height) && passed;
        bongo_cat_live2d_set_expression(app->live2d, -1);
        advance(app, 90);
        char label[32]; snprintf(label, sizeof(label), "expression-%d-reset", expression);
        passed = record(file, app, label, true, &current) &&
            close_scale(current.fit_scale, 1.0f) && !current.fitted && passed;
    }
    bongo_cat_live2d_set_dragging(app->live2d, 0.0f, 0.0f);
    bongo_cat_live2d_set_mirror(app->live2d, false);
    bongo_cat_live2d_reshape(app->live2d, width, height);
    fprintf(file, "result,1,0,0,0,0,0,0,0,0,0,%d\n", passed);
    fclose(file);
    app->dirty = true;
    return passed;
}
