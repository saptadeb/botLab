#ifndef VX_RECT_H
#define VX_RECT_H

#include "vx_object.h"
#include "vx_style.h"

#ifdef __cplusplus
extern "C" {
#endif

#define vxo_rect(...) _vxo_rect_private(__VA_ARGS__, NULL)
vx_object_t * _vxo_rect_private(vx_style_t * style, ...) __attribute__((sentinel));

#ifdef __cplusplus
}
#endif

#endif
