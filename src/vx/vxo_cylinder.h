#ifndef VXO_CYLINDER_H
#define VXO_CYLINDER_H

#include "vx_object.h"
#include "vx_style.h"

#ifdef __cplusplus
extern "C" {
#endif

// creates a cylinder with unit height (-.5,-.5) and unit *diameter*
// centered at the origin
#define vxo_cylinder(...) _vxo_cylinder_private(__VA_ARGS__, NULL)
vx_object_t * _vxo_cylinder_private(vx_style_t * style, ...) __attribute__((sentinel));

#ifdef __cplusplus
}
#endif

#endif

