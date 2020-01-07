#ifndef VXO_ARROW_H
#define VXO_ARROW_H

#include "vx_object.h"
#include "vx_style.h"

#ifdef __cplusplus
extern "C" {
#endif

// Draw an arrow of unit length, from origin to +1 on the X axis.
// Should pass a line and mesh style, in any order

#define vxo_arrow(...) _vxo_arrow_private(__VA_ARGS__, NULL)
vx_object_t * _vxo_arrow_private(vx_style_t * style, ...) __attribute__((sentinel));

#ifdef __cplusplus
}
#endif

#endif
