#ifndef VX_GL_RENDERER_H
#define VX_GL_RENDERER_H

// Note: This file does NOT implement a vx_renderer_t despite the
// similar names. It is used to much of the heavy lifting for the local renderer

// This is a non-threadsafe class which does the majority of the interaction with the gl context,
// with the following important exceptions:
//  1) does not handle any threading, so it's up to the caller to ensure it's already on the gl thread
//  2) does not handle any frame buffer object interaction. These should be configured and bound before
//     calling render()

#include "common/zhash.h"
#include "vx_code_input_stream.h"
#include "vx_code_output_stream.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef struct vx_gl_renderer vx_gl_renderer_t;

vx_gl_renderer_t * vx_gl_renderer_create();
void vx_gl_renderer_add_resources(vx_gl_renderer_t * rend, zhash_t * resources);
void vx_gl_renderer_remove_resources(vx_gl_renderer_t * rend, vx_code_input_stream_t * cins);
void vx_gl_renderer_set_buffer_render_codes(vx_gl_renderer_t * rend, vx_code_input_stream_t * cins);
void vx_gl_renderer_update_layer(vx_gl_renderer_t * rend, vx_code_input_stream_t * cins);
void vx_gl_renderer_buffer_enabled(vx_gl_renderer_t * state, vx_code_input_stream_t * cins);
// set the absolute viewport and projection, model matrix just before each render for each layer:
// XXX New interface needs fixing in android (eye), will still compile
// as is :/
void vx_gl_renderer_set_layer_render_details(vx_gl_renderer_t *rend, int layerID, const int *viewport4,
                                             const float * pm16, const float * eye3);

// returns 1 if underlying state has changed, and another render pass is required
int vx_gl_renderer_changed_since_last_render(vx_gl_renderer_t * rend);

// Returns a network-ordered output stream containing buffer codes, and resources.
vx_code_output_stream_t * vx_gl_renderer_serialize(vx_gl_renderer_t * rend);


// Must be called on gl thread:
void vx_gl_renderer_destroy(vx_gl_renderer_t * rend);
void vx_gl_renderer_draw_frame(vx_gl_renderer_t * rend, int viewport_width, int viewport_height); // execute all gl commands to draw frame

#ifdef __cplusplus
}
#endif

#endif
