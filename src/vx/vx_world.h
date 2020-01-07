#ifndef __VX_WORLD
#define __VX_WORLD

#include "common/zarray.h"

#include "vx_types.h"
#include "vx_object.h"

#ifdef __cplusplus
extern "C" {
#endif



// callback currently only occurs on buffer_swap(), maybe later on draw order change
typedef struct vx_world_listener vx_world_listener_t;

struct vx_world_listener
{
    void * impl; // typically this would be a vx_layer_t

    // Currently these functions are assumed to be thread-safe
    void (*send_codes)(vx_world_listener_t * listener, const uint8_t * data, int datalen);
    void (*send_resources)(vx_world_listener_t * listener, zhash_t * resources);
};

// NOTE: unlike vx_object and vx_resc, you must manually manage the lifetime of vx_world and vx_layer

vx_world_t * vx_world_create();
int vx_world_get_id(vx_world_t * world);

// Destroy a vx_world.
// Note: must have already destroyed all layers referencing the world
void vx_world_destroy(vx_world_t * world);

// caller is responsible to maintain 'listener' pointer until the remove call
void vx_world_add_listener(vx_world_t * world, vx_world_listener_t * listener);
void vx_world_remove_listener(vx_world_t * world, vx_world_listener_t * listener);

// returns a zarray of buffer names. caller must call zarray_vmap(za, free)
// and zarray_destroy(za) to free created resources
zarray_t *vx_world_get_buffer_list(vx_world_t * world);

vx_buffer_t * vx_world_get_buffer(vx_world_t * world, const char * name);
// XXX Currently waits to be applied on next swap() operation
void vx_buffer_set_draw_order(vx_buffer_t*, int draw_order);

void vx_buffer_add_back(vx_buffer_t * buffer, vx_object_t * obj);
void vx_buffer_swap(vx_buffer_t * buffer);

#ifdef __cplusplus
}
#endif

#endif
