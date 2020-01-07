#ifndef VXO_TETRAHEDRON_H
#define VXO_TETRAHEDRON_H

#include "vx_object.h"
#include "vx_style.h"

#ifdef __cplusplus
extern "C" {
#endif

// Draw a tetrahedron with unit-length edges, with center of mass at
// origin. Pointing in the +Z direction, base is at Z = 0
#define vxo_tetrahedron(...) _vxo_tetrahedron_private(__VA_ARGS__, NULL)
vx_object_t * _vxo_tetrahedron_private(vx_style_t * style, ...) __attribute__((sentinel));

#ifdef __cplusplus
}
#endif

#endif
