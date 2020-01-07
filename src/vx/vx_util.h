#ifndef __VX_UTIL_
#define __VX_UTIL_
#include <stdint.h>
#include "vx_object.h"

#ifdef __cplusplus
extern "C" {
#endif

uint64_t vx_util_alloc_id();

uint64_t vx_util_mtime(); // returns the current time in milliseconds

void vx_util_unproject(double * point3, double * model_matrix, double * projection_matrix, int * viewport, double * vec3_out);

void vx_util_lookat(double * eye, double * lookat, double * up, double * out44);

void vx_util_glu_perspective(double fovy_degrees, double aspect, double znear, double zfar, double * out44);
void vx_util_gl_ortho(double left, double right, double bottom, double top, double znear, double zfar, double * out44);


void vx_util_copy_floats(const double * dvals, float * fvals, int ct);
void vx_util_project(double * xyz, double * M44, double * P44, int * viewport, double * win_out3);

// XXX These maybe needs to be moved elsewhere eventually
void vx_util_quat_rotate(double * q, double * in3, double * out3);
void vx_util_quat_multiply(double * qa, double * qb, double * qout);
void vx_util_angle_axis_to_quat(double angle, double * axis3, double * qout);
void vx_util_quat_to_rpy(const double * quat4, double * rpy3_out);

// flip y coordinate from img 'data' *in place*.
// 'stride' is width of each row in bytes, e.g. width*bytes_per_pixel
// 'height' is number of rows in data.
// 'data' should point to stride*height bytes of data.
void vx_util_flipy(int stride, int height, uint8_t * data);

// Returns a placeholder vx_object_t which will result in no rendering and no render codes
vx_object_t * vx_util_blank_object();

//FYI, vx_util_color() has been replaced by vx_colors.h

#ifdef __cplusplus
}
#endif

#endif
