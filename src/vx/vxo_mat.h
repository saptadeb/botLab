#ifndef VX_MAT_H
#define VX_MAT_H

#include "vx_object.h"

#ifdef __cplusplus
extern "C" {
#endif

vx_object_t * vxo_mat_translate2(double x, double y);
vx_object_t * vxo_mat_translate3(double x, double y, double z);
vx_object_t * vxo_mat_scale(double s);
vx_object_t * vxo_mat_scale2(double sx, double sy);
vx_object_t * vxo_mat_scale3(double sx, double sy, double sz);

// Rotation is only allowed in radians
vx_object_t * vxo_mat_rotate_x(double radians);
vx_object_t * vxo_mat_rotate_y(double radians);
vx_object_t * vxo_mat_rotate_z(double radians);

vx_object_t * vxo_mat_copy_from_doubles(double * mat44);
vx_object_t * vxo_mat_quat_pos(const double quat[4], const double pos[3]);
vx_object_t * vxo_mat_from_xyzrph (double x[6]);
vx_object_t * vxo_mat_from_xyt (double xyt[3]);

// you must balance these yourself; usually only make sense in a vxo_chain.
vx_object_t *vxo_mat_push();
vx_object_t *vxo_mat_pop();

#ifdef __cplusplus
}
#endif


#endif
