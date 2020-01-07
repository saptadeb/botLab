#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <assert.h>
#include <math.h>
#include <string.h>

#include "vx/math/matd.h"
#include "common/zarray.h"
#include "vx_util.h"

static uint64_t atomicLong = 1; //XXX XXX

uint64_t vx_util_alloc_id()
{
    return __sync_fetch_and_add(&atomicLong, 1);
}


uint64_t vx_util_mtime()
{
    // get the current time
    struct timeval tv;
    gettimeofday (&tv, NULL);
    return (int64_t) tv.tv_sec * 1000 + tv.tv_usec/1000;
}


void vx_util_unproject(double * winxyz, double * model_matrix, double * projection_matrix, int * viewport, double * vec3_out)
{
    zarray_t * fp = zarray_create(sizeof(matd_t*));

    matd_t * mm = matd_create_data(4, 4, model_matrix);
    zarray_add(fp, &mm);
    matd_t * pm = matd_create_data(4, 4, projection_matrix);
    zarray_add(fp, &pm);

    matd_t *invpm = matd_op("(MM)^-1", pm, mm);
    zarray_add(fp, &invpm);

    double v[4] = { 2*(winxyz[0]-viewport[0]) / viewport[2] - 1,
                    2*(winxyz[1]-viewport[1]) / viewport[3] - 1,
                    2*winxyz[2] - 1,
                    1 };
    matd_t * vm = matd_create_data(4, 1, v);
    zarray_add(fp, &vm);

    matd_t * objxyzh = matd_op("MM", invpm, vm);
    zarray_add(fp, &objxyzh);

    vec3_out[0] = objxyzh->data[0] / objxyzh->data[3];
    vec3_out[1] = objxyzh->data[1] / objxyzh->data[3];
    vec3_out[2] = objxyzh->data[2] / objxyzh->data[3];

    // cleanup
    zarray_vmap(fp, matd_destroy);
    zarray_destroy(fp);
}


void vx_util_glu_perspective(double fovy_degrees, double aspect, double znear, double zfar, double * M)
{
    double fovy_rad = fovy_degrees * M_PI / 180.0;
    double f = 1.0 / tan(fovy_rad/2);

    M[0*4 + 0] = f/aspect;
    M[1*4 + 1] = f;
    M[2*4 + 2] = (zfar+znear)/(znear-zfar);
    M[2*4 + 3] = 2*zfar*znear / (znear-zfar);
    M[3*4 + 2] = -1;
}

void vx_util_gl_ortho(double left, double right, double bottom, double top, double znear, double zfar, double * M)
{
    M[0*4 + 0] = 2 / (right - left);
    M[0*4 + 3] = -(right+left)/(right-left);
    M[1*4 + 1] = 2 / (top-bottom);
    M[1*4 + 3] = -(top+bottom)/(top-bottom);
    M[2*4 + 2] = -2 / (zfar - znear);
    M[2*4 + 3] = -(zfar+znear)/(zfar-znear);
    M[3*4 + 3] = 1;
}

void vx_util_lookat(double * _eye, double * _lookat, double * _up, double * _out44)
{
    zarray_t * fp = zarray_create(sizeof(matd_t*));

    matd_t * eye = matd_create_data(3,1, _eye);
    zarray_add(fp, &eye);

    matd_t * lookat = matd_create_data(3,1, _lookat);
    zarray_add(fp, &lookat);

    matd_t * up = matd_create_data(3,1, _up);
    zarray_add(fp, &up);

    up = matd_vec_normalize(up);

    zarray_add(fp, &up); // note different pointer than before!

    matd_t * tmp1 = matd_subtract(lookat, eye); zarray_add(fp, &tmp1);

    matd_t * f = matd_vec_normalize(tmp1);      zarray_add(fp, &f);
    matd_t * s = matd_crossproduct(f, up);      zarray_add(fp, &s);
    matd_t * u = matd_crossproduct(s, f);       zarray_add(fp, &u);

    matd_t * M = matd_create(4,4); // set the rows of M with s, u, -f
    zarray_add(fp, &M);
    memcpy(M->data,s->data,3*sizeof(double));
    memcpy(M->data + 4,u->data,3*sizeof(double));
    memcpy(M->data + 8,f->data,3*sizeof(double));
    for (int i = 0; i < 3; i++)
        M->data[2*4 +i] *= -1;
    M->data[3*4 + 3] = 1.0;


    matd_t * T = matd_create(4,4);
    T->data[0*4 + 3] = -eye->data[0];
    T->data[1*4 + 3] = -eye->data[1];
    T->data[2*4 + 3] = -eye->data[2];
    T->data[0*4 + 0] = 1;
    T->data[1*4 + 1] = 1;
    T->data[2*4 + 2] = 1;
    T->data[3*4 + 3] = 1;
    zarray_add(fp, &T);

    matd_t * MT = matd_op("MM",M,T);
    zarray_add(fp, &MT);


    memcpy(_out44, MT->data, 16*sizeof(double));

    // cleanup
    zarray_vmap(fp, matd_destroy);
    zarray_destroy(fp);
}

void vx_util_copy_floats(const double * dvals, float * fvals, int ct)
{
    for (int i = 0; i < ct; i++)
        fvals[i] =  (float) dvals[i];
}

void vx_util_project(double * xyz, double * M44, double * P44, int * viewport, double * win_out3)
{
    zarray_t * fp = zarray_create(sizeof(matd_t*));

    matd_t * M = matd_create_data(4,4, M44); zarray_add(fp, &M);
    matd_t * P = matd_create_data(4,4, P44); zarray_add(fp, &P);
    matd_t * xyzp = matd_create(4,1); zarray_add(fp, &xyzp);
    memcpy(xyzp->data, xyz, 3*sizeof(double));
    xyzp->data[3] = 1.0;

    matd_t * p = matd_op("MMM", P, M, xyzp); zarray_add(fp, &p);

        p->data[0] = p->data[0] / p->data[3];
        p->data[1] = p->data[1] / p->data[3];
        p->data[2] = p->data[2] / p->data[3];


    double res[] =  { viewport[0] + viewport[2]*(p->data[0]+1)/2.0,
                      viewport[1] + viewport[3]*(p->data[1]+1)/2.0,
                      (viewport[2] + 1)/2.0 };
    memcpy(win_out3, res, 3*sizeof(double));

    // cleanup
    zarray_vmap(fp, matd_destroy);
    zarray_destroy(fp);
}

void vx_util_quat_rotate(double * q, double * v3, double * out3)
{
    // Ported from april.jmat.LinAlg
    double t2, t3, t4, t5, t6, t7, t8, t9, t10;

    t2 = q[0]*q[1];
    t3 = q[0]*q[2];
    t4 = q[0]*q[3];
    t5 = -q[1]*q[1];
    t6 = q[1]*q[2];
    t7 = q[1]*q[3];
    t8 = -q[2]*q[2];
    t9 = q[2]*q[3];
    t10 = -q[3]*q[3];

    out3[0] = 2*((t8+t10)*v3[0] + (t6-t4)*v3[1]  + (t3+t7)*v3[2]) + v3[0];
    out3[1] = 2*((t4+t6)*v3[0]  + (t5+t10)*v3[1] + (t9-t2)*v3[2]) + v3[1];
    out3[2] = 2*((t7-t3)*v3[0]  + (t2+t9)*v3[1]  + (t5+t8)*v3[2]) + v3[2];
}

void vx_util_quat_multiply(double * qa, double * qb, double * qout)
{
    qout[0] = qa[0]*qb[0] - qa[1]*qb[1] - qa[2]*qb[2] - qa[3]*qb[3];
    qout[1] = qa[0]*qb[1] + qa[1]*qb[0] + qa[2]*qb[3] - qa[3]*qb[2];
    qout[2] = qa[0]*qb[2] - qa[1]*qb[3] + qa[2]*qb[0] + qa[3]*qb[1];
    qout[3] = qa[0]*qb[3] + qa[1]*qb[2] - qa[2]*qb[1] + qa[3]*qb[0];
}

void vx_util_angle_axis_to_quat(double theta, double * axis3, double * qout)
{
    zarray_t * fp = zarray_create(sizeof(matd_t*));

    matd_t * axis = matd_create_data(3,1, axis3); zarray_add(fp, &axis);
    matd_t * axis_norm = matd_vec_normalize(axis); zarray_add(fp, &axis_norm);

    qout[0] = cos(theta/2);
    double s = sin(theta/2);

    qout[1] = axis_norm->data[0] * s;
    qout[2] = axis_norm->data[1] * s;
    qout[3] = axis_norm->data[2] * s;

    // cleanup
    zarray_vmap(fp, matd_destroy);
    zarray_destroy(fp);
}

void vx_util_quat_to_rpy(const double * quat4, double * rpy3_out)
{
    const double * q = quat4;

    double roll_a = 2 * (q[0]*q[1] + q[2]*q[3]);
    double roll_b = 1 - 2 * (q[1]*q[1] + q[2]*q[2]);
    rpy3_out[0] = atan2( roll_a, roll_b );

    double pitch_sin = 2 * ( q[0]*q[2] - q[3]*q[1] );
    rpy3_out[1] = asin( pitch_sin );

    double yaw_a = 2 * (q[0]*q[3] + q[1]*q[2]);
    double yaw_b = 1 - 2 * (q[2]*q[2] + q[3]*q[3]);
    rpy3_out[2] = atan2( yaw_a, yaw_b );
}

// flip y coordinate from img 'data'.
// 'stride' is width of each row in bytes, e.g. width*bytes_per_pixel
// 'height' is number of rows in data.
// 'data' should point to stride*height bytes of data.
void vx_util_flipy(int stride, int height, uint8_t * data)
{
    uint8_t tmp[stride];

    for (int rowa = 0; rowa < height/2; rowa++) {

        int rowb = height - 1 - rowa;

        // swap rowa and rowb
        memcpy(tmp, data+rowa*stride, stride); // a -> tmp
        memcpy(data+rowa*stride, data+rowb*stride, stride); // b -> a
        memcpy(data+rowb*stride, tmp, stride);// tmp -> b
    }
}

static void blank_destroy(vx_object_t * vo)
{
    free(vo);
}

static void blank_append(vx_object_t * obj, zhash_t * resources, vx_code_output_stream_t * output)
{
    // do nothing.
}


// Returns a placeholder vx_object_t which will result in no rendering and no render codes
vx_object_t * vx_util_blank_object()
{
    vx_object_t * vo = calloc(1, sizeof(vx_object_t));

    vo->destroy = blank_destroy;
    vo->append = blank_append;
    return vo;
}
