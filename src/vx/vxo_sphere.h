#ifndef VXO_SPHERE_H
#define VXO_SPHERE_H

#include "vx_object.h"
#include "vx_style.h"

#ifdef __cplusplus
extern "C" {
#endif

#define vxo_sphere(...) _vxo_sphere_private(__VA_ARGS__, NULL)
vx_object_t * _vxo_sphere_private(vx_style_t * style, ...) __attribute__((sentinel));

#ifdef __cplusplus
}
#endif

#endif
