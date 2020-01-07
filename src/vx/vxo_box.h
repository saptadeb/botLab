#ifndef VXO_BOX_H
#define VXO_BOX_H

#include "vx_object.h"
#include "vx_style.h"

#ifdef __cplusplus
extern "C" {
#endif

#define vxo_box(...) _vxo_box_private(__VA_ARGS__, NULL)
vx_object_t * _vxo_box_private(vx_style_t * style, ...) __attribute__((sentinel));

#ifdef __cplusplus
}
#endif

#endif
