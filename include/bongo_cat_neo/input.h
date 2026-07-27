#ifndef BONGO_CAT_NEO_INPUT_H
#define BONGO_CAT_NEO_INPUT_H

#include "bongo_cat_neo/common.h"
#include <stdatomic.h>

#define BONGO_CAT_NEO_INPUT_QUEUE_CAP 256u
#define BONGO_CAT_NEO_INPUT_RECOVERY_CAP 65u
#define BONGO_CAT_NEO_INPUT_KEY_STATE_CAP 256u

typedef enum BongoCatNeoInputKind {
    BONGO_CAT_NEO_INPUT_NONE,
    BONGO_CAT_NEO_INPUT_KEY_DOWN,
    BONGO_CAT_NEO_INPUT_KEY_UP,
    BONGO_CAT_NEO_INPUT_MOUSE_DOWN,
    BONGO_CAT_NEO_INPUT_MOUSE_UP,
    BONGO_CAT_NEO_INPUT_MOUSE_MOVE,
    BONGO_CAT_NEO_INPUT_GAMEPAD_BUTTON,
    BONGO_CAT_NEO_INPUT_GAMEPAD_AXIS
} BongoCatNeoInputKind;

typedef struct BongoCatNeoInputEvent {
    BongoCatNeoInputKind kind;
    uint64_t timestamp_ms;
    char name[BONGO_CAT_NEO_ID_CAP];
    float value;
    double x;
    double y;
    uint64_t sequence;
} BongoCatNeoInputEvent;

typedef struct BongoCatNeoAutoRelease {
    uint64_t deadline_ms;
    char name[BONGO_CAT_NEO_ID_CAP];
} BongoCatNeoAutoRelease;

typedef struct BongoCatNeoInputState {
    BongoCatNeoInputEvent queue[BONGO_CAT_NEO_INPUT_QUEUE_CAP];
    atomic_uint_fast16_t head;
    atomic_uint_fast16_t tail;
    _Atomic double mouse_x;
    _Atomic double mouse_y;
    atomic_bool mouse_dirty;
    atomic_uint_fast8_t control;
    atomic_uint_fast8_t shift;
    atomic_uint_fast64_t dropped;
    atomic_uint_fast64_t next_sequence;
    BongoCatNeoInputEvent recovery[BONGO_CAT_NEO_INPUT_RECOVERY_CAP];
    atomic_uint_fast8_t recovery_head;
    atomic_uint_fast8_t recovery_tail;
    BongoCatNeoAutoRelease releases[BONGO_CAT_NEO_AUTO_RELEASE_CAP];
    size_t release_count;
} BongoCatNeoInputState;

void bongo_cat_neo_input_init(BongoCatNeoInputState *state);
bool bongo_cat_neo_input_edge(bool states[BONGO_CAT_NEO_INPUT_KEY_STATE_CAP],
    unsigned code, bool down);
bool bongo_cat_neo_input_push(BongoCatNeoInputState *state, const BongoCatNeoInputEvent *event);
bool bongo_cat_neo_input_pop(BongoCatNeoInputState *state, BongoCatNeoInputEvent *event);
bool bongo_cat_neo_input_mouse(BongoCatNeoInputState *state, double x, double y);
bool bongo_cat_neo_input_take_mouse(BongoCatNeoInputState *state, double *x, double *y);
bool bongo_cat_neo_input_control_down(const BongoCatNeoInputState *state);
bool bongo_cat_neo_input_shift_down(const BongoCatNeoInputState *state);
void bongo_cat_neo_input_auto_release(BongoCatNeoInputState *state,
    const BongoCatNeoInputEvent *event, uint64_t delay_ms);
bool bongo_cat_neo_input_take_release(BongoCatNeoInputState *state, uint64_t now_ms,
    BongoCatNeoInputEvent *event);

#endif
