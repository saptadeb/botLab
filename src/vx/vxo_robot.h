#ifndef _VX_ROBOT_H
#define _VX_ROBOT_H

#include "vx_object.h"
#include "vx_style.h"

#ifdef __cplusplus
extern "C" {
#endif

#define vxo_robot(...) _vxo_robot_private(__VA_ARGS__, NULL)
vx_object_t * _vxo_robot_private(vx_style_t * style, ...) __attribute__((sentinel));

#ifdef __cplusplus
}
#endif

#endif
