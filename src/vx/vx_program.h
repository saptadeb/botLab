#ifndef __VX_PROGRAM_
#define __VX_PROGRAM_

#include "vx_object.h"
#include "vx_resc.h"

#ifdef __cplusplus
extern "C" {
#endif

// forward reference
typedef struct vx_program_state vx_program_state_t;

typedef struct vx_program vx_program_t;
struct vx_program
{
    vx_object_t *super;
    vx_program_state_t *state;
};

#define VX_PROGRAM_USE_PM 1 // default enabled
#define VX_PROGRAM_USE_M  2 // model matrix (default disabled)
#define VX_PROGRAM_USE_N  4 // normal matrix (default disabled)
#define VX_PROGRAM_USE_CAM_POS  8 // camera pos (default disabled)

// You must call library_init before the load_library call
//void vx_program_library_init();
//void vx_program_library_destroy();

vx_program_t * vx_program_load_library(char * name);

vx_program_t * vx_program_create(vx_resc_t * vert_src, vx_resc_t * frag_src);
//XXX use vx_object_dec_destroy() to destroy?

void vx_program_set_draw_array(vx_program_t * program, int count, int type);
void vx_program_set_element_array(vx_program_t * program, vx_resc_t * indices, int type);
void vx_program_set_vertex_attrib(vx_program_t * program, char * name, vx_resc_t * attrib, int dim);

// The following roughly correspond to the glUniform#<type>vector. For
// consistency, we're only implementing the pass by reference (v suffix)
// methods, even though the pass by value methods might work equally
// well for single element uniforms
void vx_program_set_uniform4fv(vx_program_t * program, char * name, const float * vec4);
void vx_program_set_uniform3fv(vx_program_t * program, char * name, const float * vec3);
void vx_program_set_uniform2fv(vx_program_t * program, char * name, const float * vec2);
void vx_program_set_uniform1fv(vx_program_t * program, char * name, const float * vec1);

void vx_program_set_uniform4iv(vx_program_t * program, char * name, const int32_t * vec4);
void vx_program_set_uniform3iv(vx_program_t * program, char * name, const int32_t * vec3);
void vx_program_set_uniform2iv(vx_program_t * program, char * name, const int32_t * vec2);
void vx_program_set_uniform1iv(vx_program_t * program, char * name, const int32_t * vec1);

void vx_program_set_texture(vx_program_t * program, char * name, vx_resc_t * vr,
                            int width, int height, int type, uint32_t flags);
void vx_program_set_line_width(vx_program_t * program, float size);

void vx_program_set_flags(vx_program_t * program, int flags);

//void vx_program_set_uniform_matrix4fv(vx_program_t * program, char * name, float * mat44);

#ifdef __cplusplus
}
#endif

#endif
