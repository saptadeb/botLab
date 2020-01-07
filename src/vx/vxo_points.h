#ifndef VX_POINTS_H
#define VX_POINTS_H

#include "vx_object.h"
#include "vx_resc.h"
#include "vx_style.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VXO_POINTS_STYLE 0xfafbab12

// This style can be passed anytime a vxo_ object requires a styling.
vx_style_t * vxo_points_style(const float * color4, int pt_size); // single color, caller owns color4 data
vx_style_t * vxo_points_style_multi_colored(vx_resc_t * colors, int pt_size); // per vertex color, each one is 4 floats, RGBA

vx_object_t * vxo_points(vx_resc_t * points, int npoints, vx_style_t * style);

// Use indexed rendering via glDrawElements. Allows reusing vertex data
vx_object_t * vxo_points_indexed(vx_resc_t * points, int npoints, vx_resc_t * indices, vx_style_t * style);

#ifdef __cplusplus
}
#endif

#endif
