#ifndef __VX_LAYER
#define __VX_LAYER

// NOTE: unlike vx_object and vx_resc, you must manually manage the lifetime of vx_world, vx_layer, vx_canvas and vx_renderer

#include "vx_types.h"
#include "vx_world.h"
#include "vx_camera_pos.h"
#include "vx_event_handler.h"
#include "vx_camera_listener.h"
#include "vx_display.h"

#ifdef __cplusplus
extern "C" {
#endif

// Each layer contains a single world, and should be attached to a single
// display. A layer corresponds to a viewport associated with a
// camera manager. Multiple layers can maintain distinct views of the
// same world.  Currently, you must call "set_display()" before calling any
// additional functions to set properties.

vx_layer_t * vx_layer_create(vx_world_t * world);
void vx_layer_destroy(vx_layer_t * layer);
int vx_layer_id(vx_layer_t * layer);

void vx_layer_set_display(vx_layer_t * vl, vx_display_t *disp);

// set the viewport specified by [x0, y0, width, height] in percent (0.0 to 1.0) of display size
void vx_layer_set_viewport_rel(vx_layer_t * vl, float * viewport4);
// set the viewport in absolute pixel sizes. Use OP_LAYER_TOP_RIGHT, etc from vx_codes.h
void vx_layer_set_viewport_abs(vx_layer_t * vl, int op_code, int width, int height);


void vx_layer_buffer_enabled(vx_layer_t * vl, const char * buffer_name, int enabled);
void vx_layer_set_background_color(vx_layer_t * vl, const float * color4);
void vx_layer_set_draw_order(vx_layer_t * vl, int draw_order);

// this layer will call eh->destroy() when vx_layer_destroy() is called
void vx_layer_add_event_handler(vx_layer_t * vl, vx_event_handler_t * eh);
void vx_layer_add_camera_listener(vx_layer_t * vl, vx_camera_listener_t * cl);

////// Camera management: ///////

// Do a single OP like OP_CAMERA_DEFAULTS, or
// OP_PROJ_PERSPECTIVE/ORTHO.
void vx_layer_camera_op(vx_layer_t *vl, int op_code);

// Fully specify the camera position:
void vx_layer_camera_lookat_timed(vx_layer_t * vl, const float * eye3, const float* lookat3,
                                  const float * up3, uint8_t set_default, uint64_t animate_ms);

void vx_layer_camera_lookat(vx_layer_t * vl, const float * eye3, const float* lookat3,
                            const float * up3, uint8_t set_default);

// Follow mode
void vx_layer_camera_follow(vx_layer_t * vl, const double * robot_pos3,
                            const double * robot_quat4, int followYaw);

// call this to cleanly disengage follow mode. Mainly useful incase
// follow mode is re-enabled later, since it prevents the old
// vehicle-cam offset from being used again.
void vx_layer_camera_follow_disable(vx_layer_t * vl);

// Do a best effort to view (from directly above) the rectangle
// encompassed by [xy0, xy1]
void vx_layer_camera_fit2D(vx_layer_t * vl, const float * xy0,
                           const float* xy1, uint8_t set_default);

#ifdef __cplusplus
}
#endif

#endif
