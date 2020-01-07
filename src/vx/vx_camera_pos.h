#ifndef VX_CAMERA_POS_H
#define VX_CAMERA_POS_H

#include "vx_types.h"
#include "vx_ray3.h"

#ifdef __cplusplus
extern "C" {
#endif

struct vx_camera_pos
{
    double eye[3];
    double lookat[3];
    double up[3];

    int viewport[4]; // absolute viewport (x0, y0, width, height). users should not change this!

    double perspectiveness;
    double perspective_fovy_degrees;
    double zclip_near;
    double zclip_far;
};

vx_camera_pos_t * vx_camera_pos_create();
void vx_camera_pos_destroy(vx_camera_pos_t * pos);
void vx_camera_pos_compute_ray(vx_camera_pos_t * pos, double winx, double winy, vx_ray3_t * out);

void vx_camera_pos_model_matrix(vx_camera_pos_t * pos, double * out44);
void vx_camera_pos_projection_matrix(vx_camera_pos_t * pos, double * out44);


#ifdef __cplusplus
}
#endif


#endif
