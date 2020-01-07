#ifndef VXO_PIX_COORDS_H
#define VXO_PIX_COORDS_H

#include "vx_object.h"

// See vx_codes.h for VX_ORIGIN_CENTER, etc

#ifdef __cplusplus
extern "C" {
#endif

vx_object_t * vxo_pix_coords(int origin_code, vx_object_t * obj);

#ifdef __cplusplus
}
#endif

#endif
