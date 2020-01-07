#ifndef VX_EVENT_H
#define VX_EVENT_H

#include <stdint.h>

#include "vx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct vx_key_event
{
    uint32_t modifiers; // see vx_codes.h
    uint32_t key_code;  // see vx_key_codes.h
    uint32_t released;  // 1 if key is released, 0 if pressed
};

struct vx_mouse_event
{
    float x, y; // NOTE: x+ is right, y+ is up
    uint32_t button_mask; // which mouse buttons were down?
    int32_t scroll_amt; // negative if away(up/left) , positive if towards(down/right)
    uint32_t modifiers;
};


// largely based on android MotionEvent
struct vx_touch_event
{
    int finger_count;
    float * x; // [finger_count];
    float * y; //
    int * ids; //

    int action_id;
    int action; // VX_TOUCH_UP, VX_TOUCH_DOWN, VX_TOUCH_MOVE
};


#ifdef __cplusplus
}
#endif

#endif
