#ifndef VX_EVENT_HANDLER_H
#define VX_EVENT_HANDLER_H

#include "vx_types.h"
#include "vx_event.h"
#include "vx_camera_pos.h"
#include "vx_layer.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Camera and event structs are read only
   and copies must be made for local storage*/
// XXX should make most of these structs const
struct vx_event_handler
{
    int dispatch_order; // determines which order the event handlers will called in. Lower numbers get first dibs

    // return 1 if event was consumed, 0 otherwise to allow other handlers access
    int (*touch_event)(vx_event_handler_t * vh, vx_layer_t * vl, vx_camera_pos_t * pos, vx_touch_event_t * mouse);
    int (*mouse_event)(vx_event_handler_t * vh, vx_layer_t * vl, vx_camera_pos_t * pos, vx_mouse_event_t * mouse);
    int (*key_event)(vx_event_handler_t * vh, vx_layer_t * vl, vx_key_event_t * key);
    void (*destroy)(vx_event_handler_t * vh);
    void * impl;
};

#ifdef __cplusplus
}
#endif

#endif
