#ifndef VXO_GRID_H
#define VXO_GRID_H

#include "vx_object.h"
#include "vx_style.h"

#ifdef __cplusplus
extern "C" {
#endif

// XXX Currently unable to do dynamic sizeing
vx_object_t * vxo_grid(void); // 1M grid by default
vx_object_t * vxo_grid_colored(vx_style_t * style);

#ifdef __cplusplus
}
#endif

#endif
