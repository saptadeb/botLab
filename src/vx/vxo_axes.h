#ifndef VXO_AXES_H
#define VXO_AXES_H

#include "vx_object.h"
#include "vx_style.h"

// renders a unit axis marker, with arrows down each axis (X, Y, Z), by
// default colors are red, green, blue, respectively

#ifdef __cplusplus
extern "C" {
#endif

vx_object_t * vxo_axes(void);
vx_object_t * vxo_axes_styled(vx_style_t * x_mesh_style, vx_style_t * y_mesh_style, vx_style_t * z_mesh_style);

#ifdef __cplusplus
}
#endif

#endif
