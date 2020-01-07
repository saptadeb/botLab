#ifndef DEFAULT_CAMERA_MGR_H
#define DEFAULT_CAMERA_MGR_H

#include "vx_camera_pos.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct default_cam_mgr default_cam_mgr_t;


default_cam_mgr_t * default_cam_mgr_create();

// returns a copy which user is responsible for destroying
vx_camera_pos_t * default_cam_mgr_get_cam_pos(default_cam_mgr_t * defmgr,
                                           int * viewport_absolute, uint64_t mtime);


void default_cam_mgr_set_cam_pos(default_cam_mgr_t * defmgr, vx_camera_pos_t *cp,
                                 int set_default, uint32_t animate_ms);

// returns the current target, and mtime (if not NULL) for this camera
vx_camera_pos_t * default_cam_mgr_get_cam_target(default_cam_mgr_t * defmgr,
                                                 uint64_t * mtime_out);

void default_cam_mgr_fit2D(default_cam_mgr_t * defmgr, double *xy0, double *xy1,
                           int set_default, uint32_t animate_ms);

void default_cam_mgr_lookat(default_cam_mgr_t * defmgr, double *eye, double *lookat,
                            double *up, int set_default, uint32_t animate_ms);

// rotate the current camera a relative amount, with a quaternion
void default_cam_mgr_rotate(default_cam_mgr_t * defmgr, double *q, uint32_t animate_ms);
void default_cam_mgr_defaults(default_cam_mgr_t * defmgr, uint32_t animate_ms);

// Repeated calls to this function will follow a particular
// robot. Use the other camera calls to setup the initial view if
// necessary
void default_cam_mgr_follow(default_cam_mgr_t * defmgr, double * pos3, double * quat4,
                            int followYaw, uint32_t animate_ms);
void default_cam_mgr_follow_disable(default_cam_mgr_t * defmgr);

void default_cam_mgr_set_interface_mode(default_cam_mgr_t * defmgr, double mode);
double default_cam_mgr_get_interface_mode(default_cam_mgr_t * defmgr);

void default_cam_mgr_destroy(default_cam_mgr_t * defmgr);

#ifdef __cplusplus
}
#endif

#endif
