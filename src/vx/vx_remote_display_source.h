#ifndef VX_REMOTE_DISPLAY_SOURCE_H
#define VX_REMOTE_DISPLAY_SOURCE_H

#include "vx_display.h"

typedef struct vx_remote_display_source vx_remote_display_source_t;

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_VX_AD_PORT 15150
#define DEFAULT_VX_CXN_PORT 15151
#define VX_AD_MAGIC 0x550166fe

typedef struct
{

    uint16_t advertise_port; // set to 0 to disable advertisements
    uint16_t connection_port; //

    char * advertise_name; // can be NULL if ad port is 0

    // Artificially limit the bandwidth:
    //  < 0  : unlimited
    //  >= 0 : limited to X kilobytes per second
    int max_bandwidth_KBs;

} vx_remote_display_source_attr_t;

// Create a new connection manager, with a callback when a new connection is established
// User should free/manager *app after this call returns
// Same as create_attr(app, NULL)
vx_remote_display_source_t * vx_remote_display_source_create(vx_application_t * app);

// Pass a reference to attr, user is responsible for freeing it.
// attr can be NULL, in which case defaults are loaded for all opts
vx_remote_display_source_t * vx_remote_display_source_create_attr(vx_application_t * app,
                                                                  const vx_remote_display_source_attr_t * attr);
// Note: should combine previous 2 functions, but separate now to
// maintain compatibility for class


void vx_remote_display_source_destroy(vx_remote_display_source_t* remote_source);


// Write the default values to 'attr'
void vx_remote_display_source_attr_init(vx_remote_display_source_attr_t * attr);
void vx_remote_display_source_attr_destroy(vx_remote_display_source_attr_t * attr);

#ifdef __cplusplus
}
#endif

#endif
