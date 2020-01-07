#ifndef VXO_SQUARE_PYRAMID_H
#define VXO_SQUARE_PYRAMID_H

#include "vx_object.h"
#include "vx_style.h"

#ifdef __cplusplus
extern "C" {
#endif

// Generate a square pyramid with unit-length edges on the base, and
// unit height pointing up the Z axes. Base is at Z = 0
#define vxo_square_pyramid(...) _vxo_square_pyramid_private(__VA_ARGS__, NULL)
vx_object_t * _vxo_square_pyramid_private(vx_style_t * style, ...) __attribute__((sentinel));

#ifdef __cplusplus
}
#endif

#endif
