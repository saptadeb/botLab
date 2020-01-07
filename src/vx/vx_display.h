#ifndef VX_DISPLAY_H
#define VX_DISPLAY_H

#include "common/zhash.h"
#include "vx/vx_camera_pos.h"
#include "vx/vx_event.h"


#ifdef __cplusplus
extern "C" {
#endif

typedef struct vx_display vx_display_t;

typedef struct vx_display_listener vx_display_listener_t;

typedef struct vx_application vx_application_t;

// User-filled struct allowing displays to be connected back to a user
struct vx_application
{
    void * impl;
    void (*display_started)(vx_application_t * app, vx_display_t * disp);
    void (*display_finished)(vx_application_t * app, vx_display_t * disp);

};

struct vx_display_listener
{
    void * impl;

    void (*viewport_changed)(vx_display_listener_t * listener, int width, int height);
    void (*event_dispatch_key)(vx_display_listener_t * listener, int layerID,
                               vx_key_event_t * event);
    void (*event_dispatch_mouse)(vx_display_listener_t * listener, int layerID,
                                 vx_camera_pos_t *pos, vx_mouse_event_t * event);
    void (*event_dispatch_touch)(vx_display_listener_t * listener, int layerID,
                                 vx_camera_pos_t *pos, vx_touch_event_t * event);
    void (*camera_changed)(vx_display_listener_t * listener, int layerID,
                           vx_camera_pos_t *pos);
};

struct vx_display
{
    int impl_type;
    void * impl;

    void (*send_codes)(vx_display_t * disp, const uint8_t * data, int datalen);
    void (*send_resources)(vx_display_t * disp, zhash_t * resources);

    void (*add_listener)(vx_display_t * disp, vx_display_listener_t * listener);
    void (*remove_listener)(vx_display_t * disp, vx_display_listener_t * listener);
};

#ifdef __cplusplus
}
#endif

// Worlds should be careful to do an add_resources(),  OP_BUFFER_CODES, OP_DEC_RESOURCES in that order
// to make sure any possible render() call has all the resources it needs
#endif
