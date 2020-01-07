#ifndef VXO_CIRCLE_H
#define VXO_CIRCLE_H

#include "vx_object.h"
#include "vx_style.h"

#ifdef __cplusplus
extern "C" {
#endif

#define vxo_circle(...) _vxo_circle_private(__VA_ARGS__, NULL)
vx_object_t * _vxo_circle_private(vx_style_t * style, ...) __attribute__((sentinel));

#ifdef __cplusplus
}
#endif


#endif
