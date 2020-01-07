#ifndef VX_CAMERA_MGR_H
#define VX_CAMERA_MGR_H

#include <stdint.h>
#include "vx_ray3.h"
#include "vx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct vx_camera_mgr {

    // returns a copy which user is responsible for destroying
    vx_camera_pos_t * (* get_camera_pos)( vx_camera_mgr_t * vxcam, int * viewport_absolute, uint64_t mtime);
    void (* set_camera_pos)(vx_camera_mgr_t *vxcam, vx_camera_pos_t *cp, int set_default, uint32_t animate_ms);

    // returns the current target, and mtime (if not NULL) for this camera
    vx_camera_pos_t * (*get_camera_target)(vx_camera_mgr_t * mgr, uint64_t * mtime_out);

    void (* fit2D)(vx_camera_mgr_t * vxcam, double *xy0, double *xy1, int set_default, uint32_t animate_ms);

    void (* lookat)(vx_camera_mgr_t * vxcam, double *eye, double *lookat, double *up, int set_default, uint32_t animate_ms);
    // rotate the current camera a relative amount, with a quaternion
    void (* rotate)(vx_camera_mgr_t * vxcam, double *q, uint32_t animate_ms);
    void (* defaults)(vx_camera_mgr_t * vxcam, uint32_t animate_ms);

    // Repeated calls to this function will follow a particular
    // robot. Use the other camera calls to setup the initial view if
    // necessary
    void (* follow)(vx_camera_mgr_t * vxcam, double * pos3, double * quat4, int followYaw, uint32_t animate_ms);
    void (* follow_disable)(vx_camera_mgr_t * vxcam);

    void (* destroy)(vx_camera_mgr_t * vxcam);
    void * impl;

};

#ifdef __cplusplus
}
#endif

#endif
