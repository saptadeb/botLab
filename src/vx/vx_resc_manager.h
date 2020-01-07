#ifndef VX_RESC_MANAGER_H
#define VX_RESC_MANAGER_H

#include "common/zhash.h"
#include "vx_display.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vx_resc_manager vx_resc_manager_t;

// This is a utility class intended for use internally by a vx_display_t
// implementation to help manage resources on the gl context. It tracks
// which resources have been sent to the gl context, and which buffers on
// which worlds are using which resources. This prevents duplicate
// transmission, and allows resources to be freed on the gl context when
// they are no longer needed

vx_resc_manager_t * vx_resc_manager_create(vx_display_t * disp);
void vx_resc_manager_destroy(vx_resc_manager_t * mgr);

// Pass in a list of resources which are new to some world, return a
// list of which ones actually need to be transmitted to avoid duplicates
zhash_t * vx_resc_manager_dedup_resources(vx_resc_manager_t * mgr, zhash_t * resources);

// Pass in a codes describing which resources are currently in use by
// a buffer. We then track which are no longer in use, and issue an
// OP_DEALLOC_RESOURCES to the display
void vx_resc_manager_buffer_resources(vx_resc_manager_t * mgr, const uint8_t * data, int datalen);

#ifdef __cplusplus
}
#endif

#endif
