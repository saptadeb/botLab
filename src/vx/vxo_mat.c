#include "vxo_mat.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vx_codes.h"
#include "vx/math/so3.h"

static void vxo_mat_append (vx_object_t * obj, zhash_t * resources, vx_code_output_stream_t * codes)
{
    codes->write_uint32(codes, OP_MODEL_MULTF);
    double * mat = obj->impl;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            codes->write_float(codes, (float) mat[i*4 +j]);
        }
    }
}

static void vxo_mat_destroy(vx_object_t *vo)
{
    free(vo->impl);
    free(vo);
}

static void vxo_mat_push_append (vx_object_t * obj, zhash_t * resources, vx_code_output_stream_t * codes)
{
    codes->write_uint32(codes, OP_MODEL_PUSH);
}

static void vxo_mat_pop_append (vx_object_t * obj, zhash_t * resources, vx_code_output_stream_t * codes)
{
    codes->write_uint32(codes, OP_MODEL_POP);
}

static void vxo_mat_pushpop_destroy(vx_object_t *vo)
{
    free(vo);
}

vx_object_t *vxo_mat_push()
{
    vx_object_t * vo = calloc(1, sizeof(vx_object_t));
    vo->destroy = vxo_mat_pushpop_destroy;
    vo->append = vxo_mat_push_append;
    vo->impl = NULL;

    return vo;
}

vx_object_t *vxo_mat_pop()
{
    vx_object_t * vo = calloc(1, sizeof(vx_object_t));
    vo->destroy = vxo_mat_pushpop_destroy;
    vo->append = vxo_mat_pop_append;
    vo->impl = NULL;

    return vo;
}


static vx_object_t * vxo_mat_create()
{
    vx_object_t * vo = calloc(1, sizeof(vx_object_t));
    vo->destroy = vxo_mat_destroy;
    vo->append = vxo_mat_append;
    vo->impl = calloc(16, sizeof(double));

    // initialize to identity
    double * mat44 = vo->impl;
    for (int i = 0; i < 4; i++)
        mat44[i*4+i] = 1.0;

    return vo;
}


vx_object_t * vxo_mat_scale(double s)
{
    return vxo_mat_scale3(s,s,s);
}

vx_object_t * vxo_mat_scale2(double sx, double sy)
{
    return vxo_mat_scale3(sx,sy,1.0);
}

vx_object_t * vxo_mat_scale3(double sx, double sy, double sz)
{
    vx_object_t * obj = vxo_mat_create();
    double * mat44 = obj->impl;
    mat44[0*4 + 0] = sx;
    mat44[1*4 + 1] = sy;
    mat44[2*4 + 2] = sz;
    return obj;
}

vx_object_t * vxo_mat_translate2(double x, double y)
{
    return vxo_mat_translate3(x,y,0);
}

vx_object_t * vxo_mat_translate3(double x, double y, double z)
{
    vx_object_t * obj = vxo_mat_create();
    double * mat44 = obj->impl;
    mat44[0*4 + 3] = x;
    mat44[1*4 + 3] = y;
    mat44[2*4 + 3] = z;
    return obj;
}

vx_object_t * vxo_mat_rotate_x(double theta)
{
    vx_object_t * obj = vxo_mat_create();
    double * mat44 = obj->impl;
    double s = sin(theta);
    double c = cos(theta);
    mat44[0*4 + 0] =  1.0;
    mat44[1*4 + 1] =  c;
    mat44[1*4 + 2] =  -s;
    mat44[2*4 + 1] =  s;
    mat44[2*4 + 2] =  c;
    return obj;
}

vx_object_t * vxo_mat_rotate_y(double theta)
{
    vx_object_t * obj = vxo_mat_create();
    double * mat44 = obj->impl;
    double s = sin(theta);
    double c = cos(theta);
    mat44[0*4 + 0] =  c;
    mat44[0*4 + 2] =  s;
    mat44[1*4 + 1] =  1.0;
    mat44[2*4 + 0] =  -s;
    mat44[2*4 + 2] =  c;
    return obj;
}

vx_object_t * vxo_mat_rotate_z(double theta)
{
    vx_object_t * obj = vxo_mat_create();
    double * mat44 = obj->impl;
    double s = sin(theta);
    double c = cos(theta);
    mat44[0*4 + 0] =  c;
    mat44[0*4 + 1] = -s;
    mat44[1*4 + 0] =  s;
    mat44[1*4 + 1] =  c;
    mat44[2*4 + 2] =  1.0;
    return obj;
}

vx_object_t * vxo_mat_copy_from_doubles(double * mat44)
{
    vx_object_t * obj = vxo_mat_create();
    memcpy(obj->impl, mat44, 16*sizeof(double));
    return obj;
}

vx_object_t * vxo_mat_quat_pos(const double quat[4], const double pos[3])
{
    vx_object_t * obj = vxo_mat_create();
    double * mat44 = obj->impl;
    double w = quat[0], x = quat[1], y = quat[2], z = quat[3];

    mat44[0*4 + 0] = w*w + x*x - y*y - z*z;
    mat44[0*4 + 1] = 2*x*y - 2*w*z;
    mat44[0*4 + 2] = 2*x*z + 2*w*y;

    mat44[1*4 + 0] = 2*x*y + 2*w*z;
    mat44[1*4 + 1] = w*w - x*x + y*y - z*z;
    mat44[1*4 + 2] = 2*y*z - 2*w*x;

    mat44[2*4 + 0] = 2*x*z - 2*w*y;
    mat44[2*4 + 1] = 2*y*z + 2*w*x;
    mat44[2*4 + 2] = w*w - x*x - y*y + z*z;

    if (pos != NULL) {
        mat44[0*4 + 3] = pos[0];
        mat44[1*4 + 3] = pos[1];
        mat44[2*4 + 3] = pos[2];
    }

    mat44[3*4 + 3] = 1;
    return obj;
}

vx_object_t* vxo_mat_from_xyzrph (double x[6])
{
    double R[3*3];
    so3_rotxyz(R, &x[3]);
    double X[4*4] = {R[0], R[1], R[2], x[0],
                     R[3], R[4], R[5], x[1],
                     R[6], R[7], R[8], x[2],
                     0.0,  0.0,  0.0,  1.0};
    return vxo_mat_copy_from_doubles(X);
}


vx_object_t* vxo_mat_from_xyt (double xyt[3])
{
    double s = sin(xyt[2]);
    double c = cos(xyt[2]);

    double T[16] = {c, -s, 0, xyt[0],
                    s,  c, 0, xyt[1],
                    0,  0, 1, 0,
                    0,  0, 0, 1};

    return vxo_mat_copy_from_doubles(T);
}
