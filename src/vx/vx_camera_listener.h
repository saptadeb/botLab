#ifndef VX_CAMERA_LISTENER_H
#define VX_CAMERA_LISTENER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Camera pos is read only
   and copy is required for local storage*/
struct vx_camera_listener
{
    void (*camera_changed)(vx_camera_listener_t * cl, vx_layer_t * vl, vx_camera_pos_t * pos);
    void (*destroy)(vx_camera_listener_t * cl);
    void * impl;
};

#ifdef __cplusplus
}
#endif


#endif
