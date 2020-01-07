#ifndef VX_DEPTH_TEST_H
#define VX_DEPTH_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vx_object.h"

// Enable/Disable gl depth test for 'obj' (and any children
// encapsulated in obj)
vx_object_t * vxo_depth_test(int enabled, vx_object_t *obj);

#ifdef __cplusplus
}
#endif

#endif
