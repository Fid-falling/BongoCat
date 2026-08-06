#include "windows_direct_input.h"
#include "bongo_cat/file.h"

#ifdef _WIN32
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

typedef struct BongoCatDirectInput {
    LPDIRECTINPUT8A input;
    LPDIRECTINPUTDEVICE8A mouse;
    FILE *audit;
    unsigned long long reads;
    unsigned long long reacquires;
    unsigned long long failures;
    long long total_x;
    long long total_y;
} BongoCatDirectInput;

void bongo_cat_windows_direct_input_destroy(BongoCatPlatform *platform) {
    BongoCatDirectInput *state = platform ? platform->relative_pointer : NULL;
    if (!state) return;
    if (state->mouse) {
        state->mouse->lpVtbl->Unacquire(state->mouse);
        state->mouse->lpVtbl->Release(state->mouse);
    }
    if (state->input) state->input->lpVtbl->Release(state->input);
    if (state->audit) {
        fprintf(state->audit,
            "summary reads=%llu reacquires=%llu failures=%llu total_x=%lld total_y=%lld\n",
            state->reads, state->reacquires, state->failures,
            state->total_x, state->total_y);
        fclose(state->audit);
    }
    free(state);
    platform->relative_pointer = NULL;
}

bool bongo_cat_windows_direct_input_create(BongoCatPlatform *platform,
    void *window) {
    if (!platform || !window) return false;
    BongoCatDirectInput *state = calloc(1, sizeof(*state));
    if (!state || FAILED(DirectInput8Create(GetModuleHandleW(NULL),
        DIRECTINPUT_VERSION, &IID_IDirectInput8A, (void **)&state->input,
        NULL))) {
        free(state);
        return false;
    }
    platform->relative_pointer = state;
    HRESULT result = state->input->lpVtbl->CreateDevice(state->input,
        &GUID_SysMouse, &state->mouse, NULL);
    if (SUCCEEDED(result)) result = state->mouse->lpVtbl->SetCooperativeLevel(
        state->mouse, (HWND)window, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
    if (SUCCEEDED(result)) result = state->mouse->lpVtbl->SetDataFormat(
        state->mouse, &c_dfDIMouse);
    DIPROPDWORD property = {{sizeof(property), sizeof(property.diph),
        0, DIPH_DEVICE}, 16};
    if (SUCCEEDED(result)) result = state->mouse->lpVtbl->SetProperty(
        state->mouse, DIPROP_BUFFERSIZE, &property.diph);
    if (SUCCEEDED(result)) result = state->mouse->lpVtbl->Acquire(state->mouse);
    if (SUCCEEDED(result)) {
        const char *audit_path = getenv("BONGO_CAT_DIRECT_INPUT_AUDIT_FILE");
        if (audit_path && audit_path[0]) {
            state->audit = bongo_cat_file_open(audit_path, "wb");
            if (state->audit) {
                fprintf(state->audit, "direct_input initialized=1\n");
                fflush(state->audit);
            }
        }
        return true;
    }
    bongo_cat_windows_direct_input_destroy(platform);
    return false;
}

bool bongo_cat_platform_relative_pointer(BongoCatPlatform *platform,
    double *x, double *y) {
    BongoCatDirectInput *state = platform ? platform->relative_pointer : NULL;
    if (!state || !state->mouse || !x || !y) return false;
    DIMOUSESTATE mouse = {0};
    HRESULT result = state->mouse->lpVtbl->GetDeviceState(
        state->mouse, sizeof(mouse), &mouse);
    bool reacquired = false;
    if (FAILED(result)) {
        result = state->mouse->lpVtbl->Acquire(state->mouse);
        reacquired = SUCCEEDED(result);
        if (reacquired) result = state->mouse->lpVtbl->GetDeviceState(
            state->mouse, sizeof(mouse), &mouse);
    }
    state->reads++;
    if (reacquired) state->reacquires++;
    if (SUCCEEDED(result)) {
        *x = mouse.lX;
        *y = mouse.lY;
        state->total_x += mouse.lX;
        state->total_y += mouse.lY;
    } else {
        // Mver never switches back to absolute coordinates after enabling
        // DirectInput. A transient loss therefore means no new movement.
        *x = 0.0;
        *y = 0.0;
        state->failures++;
    }
    if (state->audit) {
        fprintf(state->audit,
            "read=%llu result=0x%08lx reacquired=%d dx=%.0f dy=%.0f total_x=%lld total_y=%lld\n",
            state->reads, (unsigned long)result, reacquired, *x, *y,
            state->total_x, state->total_y);
        fflush(state->audit);
    }
    // The return value describes whether relative mode is available, not
    // whether this individual sample succeeded.
    return true;
}
#endif
