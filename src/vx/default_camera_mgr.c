#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "vx_camera_pos.h"
#include "vx_util.h"
#include "vx/math/matd.h"

#include "default_camera_mgr.h"

typedef struct
{
    double xy0[2];
    double xy1[2];
    uint64_t mtime;
    uint8_t set_default;
}fit_t;


struct default_cam_mgr
{
    double saved_eye[3];
    double saved_lookat[3];
    double saved_up[3];

    // Queue a fit2D till render time:
    fit_t * fit; // Usually NULL

    // our current view is somewhere between these two, based on time:
    double eye0[3];
    double lookat0[3];
    double up0[3];
    double perspectiveness0;
    uint64_t mtime0;

    double eye1[3];
    double lookat1[3];
    double up1[3];
    double perspectiveness1;
    uint64_t mtime1;

    double perspective_fovy_degrees;
    double zclip_near;
    double zclip_far;

    double interface_mode;

    // State for follow mode.
    // XXX: We need to manage cleanup of these if follow is
    // enabled/disabled multiple times.
    // e.g. These should be set to null anytime a non-follow function is
    // called, or possibly add a new function to disable follow mode
    matd_t * follow_lastpos;
    matd_t * follow_lastquat;
};


static void scaled_combination(double * v1, double s1, double * v2, double s2, double * out, int len)
{
    for (int i = 0; i < len; i++)
        out[i] = v1[i]*s1 + v2[i]*s2;
}


static void default_destroy_fit(default_cam_mgr_t * state)
{
    fit_t * f = state->fit;
    state->fit = NULL;

    free(f);
}

void default_cam_mgr_follow_disable(default_cam_mgr_t * state)
{
    if (state->follow_lastpos != NULL) {
        matd_destroy(state->follow_lastpos);
        matd_destroy(state->follow_lastquat);
    }

    state->follow_lastpos = NULL;
    state->follow_lastquat = NULL;

}

vx_camera_pos_t * default_cam_mgr_get_cam_pos(default_cam_mgr_t * state, int * viewport, uint64_t mtime)
{
    vx_camera_pos_t * p = calloc(1, sizeof(vx_camera_pos_t));
    memcpy(p->viewport, viewport, 4*sizeof(int));

    p->perspective_fovy_degrees = state->perspective_fovy_degrees;
    p->zclip_near = state->zclip_near;
    p->zclip_far = state->zclip_far;

    // process a fit command if necessary:
    if (state->fit != NULL) {
        fit_t * f = state->fit;

        // consume the fit command
        state->fit = NULL; // XXX minor race condition, could lose a fit cmd

        // XXX We can probably do better than this using the viewport...
        state->lookat1[0] = (f->xy0[0] + f->xy1[0]) / 2;
        state->lookat1[1] = (f->xy0[1] + f->xy1[1]) / 2;
        state->lookat1[2] = 0;

        // dimensions of fit
        double Fw = f->xy1[0] - f->xy0[0];
        double Fh = f->xy1[1] - f->xy0[1];

        // aspect ratios
        double Far = Fw / Fh;
        double Var = p->viewport[2] * 1.0 / p->viewport[3];

        double tAngle = tan(p->perspective_fovy_degrees/2*M_PI/180.0);
        double height = fabs(0.5 * (Var > Far ? Fh : Fw / Var) / tAngle);

        state->eye1[0] = state->lookat1[0];
        state->eye1[1] = state->lookat1[1];
        state->eye1[2] = height;

        state->up1[0] = 0;
        state->up1[1] = 1;
        state->up1[2] = 0;

        state->mtime1 = f->mtime;

        free(f);
    }

    if (mtime > state->mtime1) {
        memcpy(p->eye, state->eye1, 3*sizeof(double));
        memcpy(p->up, state->up1, 3*sizeof(double));
        memcpy(p->lookat, state->lookat1, 3*sizeof(double));
        p->perspectiveness = state->perspectiveness1;
    } else  if (mtime <= state->mtime0) {
        memcpy(p->eye, state->eye0, 3*sizeof(double));
        memcpy(p->up, state->up0, 3*sizeof(double));
        memcpy(p->lookat, state->lookat0, 3*sizeof(double));
        p->perspectiveness = state->perspectiveness0;
    } else {
        double alpha1 = ((double) mtime - state->mtime0) / (state->mtime1 - state->mtime0);
        double alpha0 = 1.0 - alpha1;

        scaled_combination(state->eye0,    alpha0, state->eye1,    alpha1, p->eye,    3);
        scaled_combination(state->up0,     alpha0, state->up1,     alpha1, p->up,     3);
        scaled_combination(state->lookat0, alpha0, state->lookat1, alpha1, p->lookat, 3);
        p->perspectiveness = state->perspectiveness0*alpha0 + state->perspectiveness1*alpha1;

        // Tweak so eye-to-lookat is the right distance
        {
            zarray_t * fp = zarray_create(sizeof(matd_t*));

            matd_t * eye = matd_create_data(3,1, p->eye); zarray_add(fp, &eye);
            matd_t * lookat = matd_create_data(3,1, p->lookat); zarray_add(fp, &lookat);
            matd_t * up = matd_create_data(3,1, p->up); zarray_add(fp, &up);

            matd_t * eye0 = matd_create_data(3,1, state->eye0); zarray_add(fp, &eye0);
            matd_t * lookat0 = matd_create_data(3,1, state->lookat0); zarray_add(fp, &lookat0);
            matd_t * up0 = matd_create_data(3,1, state->up0); zarray_add(fp, &up0);

            matd_t * eye1 = matd_create_data(3,1, state->eye1); zarray_add(fp, &eye1);
            matd_t * lookat1 = matd_create_data(3,1, state->lookat1); zarray_add(fp, &lookat1);
            matd_t * up1 = matd_create_data(3,1, state->up1); zarray_add(fp, &up1);


            double dist0 = matd_vec_dist(eye0, lookat0);
            double dist1 = matd_vec_dist(eye1, lookat1);

            matd_t * dist = matd_create_scalar(dist0*alpha0 + dist1*alpha1); zarray_add(fp, &dist);

            matd_t * eye2p = matd_subtract(eye,lookat); zarray_add(fp, &eye2p);
            eye2p = matd_vec_normalize(eye2p); zarray_add(fp, &eye2p);

            eye = matd_op("M + (M*M)", lookat, eye2p, dist);

            // Only modified eye
            memcpy(p->eye, eye->data, 3*sizeof(double));

            zarray_vmap(fp, matd_destroy);

            zarray_destroy(fp);
        }
    }

    // Need to do more fixup depending on interface mode!
    {
        if (state->interface_mode <= 2.0) {
            // stack eye on lookat:
            p->eye[0] = p->lookat[0];
            p->eye[1] = p->lookat[1];
            p->lookat[2] = 0;

            // skip fabs() for ENU/NED compat
            //p->eye[2] = fabs(p->eye[2]);


            {
                matd_t * up = matd_create_data(3,1, p->up);
                up->data[2] = 0; // up should never point in Z
                matd_t * up_norm = matd_vec_normalize(up);

                memcpy(p->up, up_norm->data, sizeof(double)*3);
                matd_destroy(up);
                matd_destroy(up_norm);
            }

        } else if (state->interface_mode == 2.5) {
            zarray_t * fp = zarray_create(sizeof(matd_t*));

            matd_t * eye = matd_create_data(3,1, p->eye); zarray_add(fp, &eye);
            matd_t * lookat = matd_create_data(3,1, p->lookat); zarray_add(fp, &lookat);
            matd_t * up = matd_create_data(3,1, p->up); zarray_add(fp, &up);

            lookat->data[2] = 0.0;

            // Level horizon
            matd_t * dir = matd_subtract(lookat, eye); zarray_add(fp, &dir);
            matd_t * dir_norm = matd_vec_normalize(dir); zarray_add(fp, &dir_norm);
            matd_t * left = matd_crossproduct(up, dir_norm); zarray_add(fp, &left);
            left->data[2] = 0.0;

            left = matd_vec_normalize(left); zarray_add(fp, &left);


            // Don't allow upside down
            //up->data[2] = fmax(0.0, up->data[2]); // XXX NED?

            // Find an 'up' direction perpendicular to left
            matd_t * dot_scalar = matd_create_scalar(matd_vec_dot_product(up, left)); zarray_add(fp, &dot_scalar);
            up = matd_op("M - (M*M)", up,  left, dot_scalar); zarray_add(fp, &up);
            up = matd_vec_normalize(up); zarray_add(fp, &up);

            // Now find eye position by computing new lookat dir
            matd_t * eye_dir = matd_crossproduct(up, left); zarray_add(fp, &eye_dir);

            matd_t *eye_dist_scalar = matd_create_scalar(matd_vec_dist(eye, lookat)); zarray_add(fp, &eye_dist_scalar);

            eye = matd_op("M + (M*M)", lookat, eye_dir, eye_dist_scalar); zarray_add(fp, &eye);

            // export results back to p:
            memcpy(p->eye, eye->data, sizeof(double)*3);
            memcpy(p->lookat, lookat->data, sizeof(double)*3);
            memcpy(p->up, up->data, sizeof(double)*3);

            zarray_vmap(fp, matd_destroy);
            zarray_destroy(fp);
        }
    }

    // Fix up for bad zoom
    if (1) {
        matd_t * eye = matd_create_data(3,1, p->eye);
        matd_t * lookat = matd_create_data(3,1, p->lookat);
        matd_t * up = matd_create_data(3,1, p->up);

        matd_t * lookeye = matd_subtract(lookat, eye);
        matd_t * lookdir = matd_vec_normalize(lookeye);
        double dist =  matd_vec_dist(eye, lookat);
        dist  = fmin(state->zclip_far / 3.0, dist);
        dist  = fmax(state->zclip_near * 3.0, dist);

        matd_scale_inplace(lookdir, dist);

        matd_t * eye_fixed = matd_subtract(lookat, lookdir);

        memcpy(p->eye, eye_fixed->data, sizeof(double)*3);

        matd_destroy(eye);
        matd_destroy(lookat);
        matd_destroy(up);
        matd_destroy(lookeye);
        matd_destroy(lookdir);
        matd_destroy(eye_fixed);
    }

    // copy the result back into 'state'
    {
        memcpy(state->eye0, p->eye, 3*sizeof(double));
        memcpy(state->up0, p->up, 3*sizeof(double));
        memcpy(state->lookat0, p->lookat, 3*sizeof(double));
        state->perspectiveness0 = p->perspectiveness;
        state->mtime0 = mtime;
    }

    return p;
}

// save so we can restore defaults if necessary
static void save_target_pos(default_cam_mgr_t * state)
{
    memcpy(state->saved_eye, state->eye1, 3*sizeof(double));
    memcpy(state->saved_lookat, state->lookat1, 3*sizeof(double));
    memcpy(state->saved_up, state->up1, 3*sizeof(double));
}

void default_cam_mgr_defaults(default_cam_mgr_t * state, uint32_t animate_ms)
{
    memcpy(state->eye1, state->saved_eye, 3*sizeof(double));
    memcpy(state->lookat1, state->saved_lookat, 3*sizeof(double));
    memcpy(state->up1, state->saved_up, 3*sizeof(double));
    state->mtime1 = vx_util_mtime() + animate_ms;

    // Disable any prior fit command
    default_destroy_fit(state);
}

vx_camera_pos_t * default_cam_mgr_get_cam_target(default_cam_mgr_t * state, uint64_t * mtime_out)
{
    vx_camera_pos_t * cp = calloc(1, sizeof(vx_camera_pos_t));
    memcpy(cp->eye,    state->eye1,    3*sizeof(double));
    memcpy(cp->lookat, state->lookat1, 3*sizeof(double));
    memcpy(cp->up,     state->up1,     3*sizeof(double));

    cp->perspectiveness         = state->perspectiveness1;
    cp->perspective_fovy_degrees = state->perspective_fovy_degrees;
    cp->zclip_near               = state->zclip_near;
    cp->zclip_far                = state->zclip_far;

    if (mtime_out != NULL)
        *mtime_out = state->mtime1;

    return cp;
}

void default_cam_mgr_set_cam_pos(default_cam_mgr_t * state, vx_camera_pos_t *cp, int set_default, uint32_t animate_ms)
{
    memcpy(state->eye1,    cp->eye,    3*sizeof(double));
    memcpy(state->lookat1, cp->lookat, 3*sizeof(double));
    memcpy(state->up1,     cp->up,     3*sizeof(double));

    state->perspectiveness1         = cp->perspectiveness;
    state->perspective_fovy_degrees = cp->perspective_fovy_degrees;
    state->zclip_near               = cp->zclip_near;
    state->zclip_far                = cp->zclip_far;

    //JS: if we don't animate, lets just not change this.
    state->mtime1 = vx_util_mtime() + animate_ms;

    default_cam_mgr_follow_disable(state);

    if (set_default)
        save_target_pos(state);
}

void default_cam_mgr_rotate(default_cam_mgr_t * state, double *q, uint32_t animate_ms)
{
    zarray_t * fp = zarray_create(sizeof(matd_t*));

    matd_t * eye = matd_create_data(3,1,state->eye1);       zarray_add(fp, &eye);
    matd_t *lookat = matd_create_data(3,1,state->lookat1);  zarray_add(fp, &lookat);
    matd_t *up = matd_create_data(3,1,state->up1);          zarray_add(fp, &up);


    matd_t * toEye = matd_subtract(eye, lookat); zarray_add(fp, &toEye);
    matd_t * nextToEye = matd_create(3,1); zarray_add(fp, &nextToEye);
    vx_util_quat_rotate(q, toEye->data, nextToEye->data);

    matd_t * nextEye = matd_add(lookat, nextToEye); zarray_add(fp, &nextEye);
    matd_t * nextUp = matd_copy(up); zarray_add(fp, &nextUp);

    vx_util_quat_rotate(q, up->data, nextUp->data);

    // copy back results
    memcpy(state->eye1, nextEye->data, sizeof(double)*3);
    memcpy(state->up1, nextUp->data, sizeof(double)*3);
    state->mtime1 = vx_util_mtime() + animate_ms;

    // Disable any prior fit command
    default_destroy_fit(state);

    // cleanup
    zarray_vmap(fp, matd_destroy);
    zarray_destroy(fp);

    default_cam_mgr_follow_disable(state);

}

// Allow internal calls to this function which do not disable follow mode
static void internal_lookat(default_cam_mgr_t * state, double *eye, double *lookat, double *up, uint32_t animate_ms)
{
    memcpy(state->eye1, eye, 3*sizeof(double));
    memcpy(state->lookat1, lookat, 3*sizeof(double));
    memcpy(state->up1, up, 3*sizeof(double));
    state->mtime1 = vx_util_mtime() + animate_ms;
}

void default_cam_mgr_lookat(default_cam_mgr_t * state, double *eye, double *lookat, double *up, int set_default, uint32_t animate_ms)
{
    internal_lookat(state, eye, lookat, up, animate_ms);

    // Disable any prior fit command
    default_destroy_fit(state);

    default_cam_mgr_follow_disable(state);

    if (set_default)
        save_target_pos(state);

}

void default_cam_mgr_fit2D(default_cam_mgr_t * state, double *xy0, double *xy1, int set_default, uint32_t animate_ms)
{
    fit_t * fit = calloc(1,sizeof(fit_t));
    fit->mtime = vx_util_mtime() + animate_ms;

    fit->xy0[0] = xy0[0];
    fit->xy0[1] = xy0[1];

    fit->xy1[0] = xy1[0];
    fit->xy1[1] = xy1[1];

    state->fit = fit;

    if (set_default)
        save_target_pos(state);

    default_cam_mgr_follow_disable(state);
}

void default_cam_mgr_follow(default_cam_mgr_t * state, double * _pos3, double * _quat4, int followYaw, uint32_t animate_ms)
{
    matd_t * pos3 = matd_create_data(3, 1, _pos3);
    matd_t * quat4 = matd_create_data(4, 1, _quat4);

    if (state->follow_lastpos != NULL) {

        matd_t * eye1    = matd_create_data(3,1,state->eye1);
        matd_t * lookat1    = matd_create_data(3,1, state->lookat1);
        matd_t * up1    = matd_create_data(3,1, state->up1);

        if (followYaw) {

            matd_t * v2eye = matd_subtract(state->follow_lastpos, eye1);
            matd_t * v2look = matd_subtract(state->follow_lastpos, lookat1);

            matd_t * new_rpy = matd_create(3,1);
            //matd_quat_to_rpy(quat4);
            vx_util_quat_to_rpy(quat4->data, new_rpy->data);

            matd_t * last_rpy = matd_create(3,1);
            //matd_quat_to_rpy(state->follow_lastquat);
            vx_util_quat_to_rpy(state->follow_lastquat->data, last_rpy->data);

            double dtheta = new_rpy->data[2] - last_rpy->data[2];

            matd_t * zaxis = matd_create(3,1);
            zaxis->data[2] = 1;
            matd_t * zq = matd_create(4,1);
            //zq = matd_angle_axis_to_quat(dtheta, zaxis);
            vx_util_angle_axis_to_quat(dtheta, zaxis->data, zq->data);

            matd_t * v2look_rot = matd_create(3,1);
            //matd_quat_rotate(zq, v2look);
            vx_util_quat_rotate(zq->data, v2look->data, v2look_rot->data);
            matd_t * new_lookat = matd_subtract(pos3, v2look_rot);

            matd_t * v2eye_rot = matd_create(3,1);
            //matd_quat_rotate(zq, v2eye);
            vx_util_quat_rotate(zq->data, v2eye->data, v2eye_rot->data);

            matd_t * new_eye = matd_subtract(pos3, v2eye_rot);

            matd_t * new_up = matd_create(3,1);
            //matd_quat_rotate(zq, up1);
            vx_util_quat_rotate(zq->data, up1->data, new_up->data);

            internal_lookat(state, new_eye->data, new_lookat->data, new_up->data, animate_ms);

            // cleanup
            matd_destroy(v2eye);
            matd_destroy(v2look);

            matd_destroy(new_rpy);
            matd_destroy(last_rpy);

            matd_destroy(zaxis);
            matd_destroy(zq);

            matd_destroy(v2look_rot);
            matd_destroy(new_lookat);

            matd_destroy(v2eye_rot);
            matd_destroy(new_eye);

            matd_destroy(new_up);

        } else {

            matd_t * dpos   = matd_subtract(pos3, state->follow_lastpos);

            matd_t * newEye = matd_add(eye1, dpos);
            matd_t * newLookAt = matd_add(lookat1, dpos);


            internal_lookat(state, newEye->data, newLookAt->data, up1->data, animate_ms);


            matd_destroy(dpos);
            matd_destroy(newEye);
            matd_destroy(newLookAt);
        }


        matd_destroy(eye1);
        matd_destroy(lookat1);
        matd_destroy(up1);


        // Cleanup
        matd_destroy(state->follow_lastpos);
        matd_destroy(state->follow_lastquat);
    }


    state->follow_lastpos = pos3;
    state->follow_lastquat = quat4;

}

void default_cam_mgr_set_interface_mode(default_cam_mgr_t * defmgr, double mode)
{
    defmgr->interface_mode = mode;
}

double default_cam_mgr_get_interface_mode(default_cam_mgr_t * defmgr)
{
    return defmgr->interface_mode;
}

void default_cam_mgr_destroy(default_cam_mgr_t * state)
{
    // As of this writing, all memory in the manager
    // is statically allocated
    free(state);
}

default_cam_mgr_t * default_cam_mgr_create()
{

    default_cam_mgr_t * state = calloc(1, sizeof(default_cam_mgr_t));
    state->eye1[2] = 100.0f;
    state->up1[1] = 1.0f;
    state->mtime1 = vx_util_mtime();

    save_target_pos(state);

    state->interface_mode = 3.0;
    state->perspective_fovy_degrees = 50;
    state->zclip_near = 0.025;
    state->zclip_far = 50000;
    state->perspectiveness1 = 1.0;

    return state;
}
