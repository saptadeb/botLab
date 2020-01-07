#ifndef VXP_H
#define VXP_H

#include "vx_object.h"
#include "vx_resc.h"

#ifdef __cplusplus
extern "C" {
#endif

// This is a set of barebones wrappers around some of the core shader objects. It is not recommended
// to use these directly. Currently commented out to discourage use. You can copy these function defs to
// your program if you really need them
 /*
wvx_object_t * vxp_single_color(int npoints, vx_resc_t * points, float * color4, float pt_size, int type);
vx_object_t * vxp_multi_colored(int npoints, vx_resc_t * points, vx_resc_t * colors, float pt_size, int type);

vx_object_t * vxp_single_color_indexed(int npoints, vx_resc_t * points, float * color4, float pt_size, int type, vx_resc_t * indices);
vx_object_t * vxp_multi_colored_indexed(int npoints, vx_resc_t * points, vx_resc_t * colors, float pt_size, int type, vx_resc_t * indices);

vx_object_t * vxp_texture(int npoints, vx_resc_t * points, vx_resc_t * texcoords, vx_resc_t * tex, int width, int height, int format, int type, vx_resc_t * indices);
 */

#ifdef __cplusplus
}
#endif

#endif
