#ifndef VXO_TRIANGLE_H_
#define VXO_TRIANGLE_H_

#include "vx_object.h"
#include "vx_style.h"

#ifdef __cplusplus
extern "C" {
#endif

// Distance from center of triangle to lower baseline, and upper vertex:

#define VXO_TRIANGLE_EQUILATERAL_LOWER 0.25f // sin(30)/2
#define VXO_TRIANGLE_EQUILATERAL_UPPER 0.43301270f // cos(30)/2

// equilateral triangle of side length 1
// base along x axis, top pointing along +y
#define vxo_triangle(...) _vxo_triangle_private(__VA_ARGS__, NULL)
vx_object_t * _vxo_triangle_private(vx_style_t * style, ...) __attribute__((sentinel));

#ifdef __cplusplus
}
#endif

#endif /* VXO_TRIANGLE_H_ */
