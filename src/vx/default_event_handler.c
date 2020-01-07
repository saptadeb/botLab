#include "default_event_handler.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "common/zarray.h"
#include "vx/math/matd.h"
#include "vx/math/math_util.h"

#include "vx_codes.h"
#include "vx_util.h"
#include "vx_event.h"
#include "vx_camera_mgr.h"
#include <math.h>

typedef struct {
    int init_last;
    vx_mouse_event_t last_mouse;

    zhash_t * bookmarks; // < int, vx_camera_pos_t*>

    int ui_animate_ms;

    // Camera manipulation state.
    int manip_in_progress;
    double last_rotate_x, last_rotate_y;
    double manipulation_point[3];

    default_cam_mgr_t * cam_mgr;

} default_event_handler_t;

#define EL(m, row,col) (m)->data[((row)*(m)->ncols + (col))]

// How does the projected (opengl window coordinate) position of 3D point
// xyz change with respect to moving the eye and lookat positions
// by d, where d = lambda1 * dir1 + lambda2 * dir.
//
// J = [   d project(xyz)_x / d lambda1         d project(xyz)_x / d lambda2
//     [   d project(xyz)_y / d lambda1         d project(xyz)_y / d lambda2
static void compute_pan_jacobian(double *xyz, matd_t *P, int *viewport, matd_t *eye0, matd_t *lookat0, matd_t *up,
                                 matd_t *dir1, matd_t *dir2, zarray_t *fp, matd_t *J)
{
    double eps = fmax(0.00001, 0.00001*matd_vec_mag(eye0));
    matd_t *sdir1 = matd_scale(dir1, eps); zarray_add(fp, &sdir1);
    matd_t *sdir2 = matd_scale(dir2, eps); zarray_add(fp, &sdir2);

    matd_t * M0 = matd_create(4,4); zarray_add(fp, &M0);
    matd_t * w0 = matd_create(3,1); zarray_add(fp, &w0);
    vx_util_lookat(eye0->data, lookat0->data, up->data, M0->data);
    vx_util_project(xyz, M0->data, P->data, viewport, w0->data);


    matd_t * M1 = matd_create(4,4); zarray_add(fp, &M1);
    matd_t * w1 = matd_create(3,1); zarray_add(fp, &w1);
    matd_t * eye1 = matd_add(eye0, sdir1); zarray_add(fp, &eye1);
    matd_t * lookat1 = matd_add(lookat0, sdir1); zarray_add(fp, &lookat1);

    vx_util_lookat(eye1->data, lookat1->data, up->data, M1->data);
    vx_util_project(xyz, M1->data, P->data, viewport, w1->data);

    matd_t * M2 = matd_create(4,4); zarray_add(fp, &M2);
    matd_t * w2 = matd_create(3,1); zarray_add(fp, &w2);
    matd_t * eye2 = matd_add(eye0, sdir2); zarray_add(fp, &eye2);
    matd_t * lookat2 = matd_add(lookat0, sdir2); zarray_add(fp, &lookat2);

    vx_util_lookat(eye2->data, lookat2->data, up->data, M2->data);
    vx_util_project(xyz, M2->data, P->data, viewport, w2->data);

    EL(J, 0, 0) = (w1->data[0] - w0->data[0]) / eps;
    EL(J, 0, 1) = (w2->data[0] - w0->data[0]) / eps;

    EL(J, 1, 0) = (w1->data[1] - w0->data[1]) / eps;
    EL(J, 1, 1) = (w2->data[1] - w0->data[1]) / eps;
}


static void window_space_pan_to(double * xyz, double winx, double winy, matd_t * pm, int * viewport,
                                matd_t * _eye, matd_t *_lookat, matd_t *_up, matd_t *_mv)
{
    zarray_t * fp = zarray_create(sizeof(matd_t*));
    matd_t *mv = matd_create(3,1);        zarray_add(fp, &mv);
    matd_t * eye = matd_copy(_eye);       zarray_add(fp, &eye);
    matd_t *lookat = matd_copy(_lookat);  zarray_add(fp, &lookat);
    matd_t *up = matd_copy(_up);          zarray_add(fp, &up);

    for (int iter = 0; iter < 100; iter++) {

        matd_t * M = matd_create(4,4); zarray_add(fp, &M);

        vx_util_lookat(eye->data, lookat->data, up->data, M->data);
        matd_t * winpos0 = matd_create(3,1); zarray_add(fp, &winpos0);
        vx_util_project(xyz, M->data, pm->data, viewport, winpos0->data);

        matd_t * err2 = matd_create(2,1); zarray_add(fp, &err2);
        err2->data[0] = winx - winpos0->data[0];
        err2->data[1] = winy - winpos0->data[1];

        matd_t * look_vec_u = matd_subtract(lookat, eye);   zarray_add(fp, &look_vec_u);
        matd_t * look_vec = matd_vec_normalize(look_vec_u); zarray_add(fp, &look_vec);
        matd_t * left = matd_crossproduct(up, look_vec);     zarray_add(fp, &left);

        matd_t * dir1 = matd_copy(up); zarray_add(fp, &dir1);
        matd_t * dir2 = matd_copy(left); zarray_add(fp, &dir2);

        matd_t * J = matd_create(2,2); zarray_add(fp, &J);
        compute_pan_jacobian(xyz, pm, viewport, eye, lookat, up, dir1, dir2, fp, J);

        dir1->data[2] = dir2->data[2] = 0.0; // clear z

        matd_t * J_inv = matd_inverse(J);  zarray_add(fp, &J_inv);
        matd_t * weights = matd_multiply(J_inv, err2); zarray_add(fp, &weights);

        matd_t *sdir1 = matd_scale(dir1, weights->data[0]); zarray_add(fp, &sdir1);
        matd_t *sdir2 = matd_scale(dir2, weights->data[1]); zarray_add(fp, &sdir2);
        matd_t *dx = matd_add(sdir1, sdir2); zarray_add(fp, &dx);

        eye = matd_add(eye, dx); zarray_add(fp, &eye); // new ptr
        lookat = matd_add(lookat, dx); zarray_add(fp, &lookat); // new ptr
        mv = matd_add(mv, dx); zarray_add(fp, &mv); // new ptr

        if (matd_vec_mag(dx) < 0.001)
            break;
    }

    // write the result
    memcpy(_mv->data, mv->data, 3*sizeof(double));

    // cleanup
    zarray_vmap(fp, matd_destroy);
    zarray_destroy(fp);
}



static int default_mouse_event(vx_event_handler_t * vh, vx_layer_t * vl, vx_camera_pos_t * pos, vx_mouse_event_t * mouse)
{
    /* printf("mouse_event deh (%f,%f) buttons = %x scroll = %d modifiers = %x\n", */
    /*        mouse->xy[0],mouse->xy[1], mouse->button_mask, mouse->scroll_amt, mouse->modifiers); */

    default_event_handler_t * deh = (default_event_handler_t*) vh->impl;

    int shift = mouse->modifiers & VX_SHIFT_MASK;
    /* int ctrl = mouse->modifiers & VX_CTRL_MASK; */


    // Stack allocate the most recent mouse event, so we can return from this function at any time
    vx_mouse_event_t last_mouse;
    if (deh->init_last) {
        memcpy(&last_mouse, &deh->last_mouse, sizeof(vx_mouse_event_t));
        memcpy(&deh->last_mouse, mouse, sizeof(vx_mouse_event_t));
    } else {
        memcpy(&deh->last_mouse, mouse, sizeof(vx_mouse_event_t));
        deh->init_last = 1;
        return 0;
    }

    int diff_button = mouse->button_mask ^ last_mouse.button_mask;
    //int button_down = diff_button & mouse->button_mask; // which button(s) just got pressed?
    int button_up = diff_button & last_mouse.button_mask; // which button(s) just got released?


    // Disable movement of the camera due to mouse motion with the
    // following modifiers
    if (!(mouse->modifiers & VX_SHIFT_MASK) &&
        !(mouse->modifiers & VX_CTRL_MASK) &&
        !(mouse->modifiers & VX_ALT_MASK )) {

        // PART 1: "Mouse Down", or entering manip transition.
        // could either happen if a button is pushed, or if a modifier is
        // released and the button is already down
        if (!deh->manip_in_progress) {
            if (mouse->button_mask == VX_BUTTON1_MASK) { // panning
                deh->manip_in_progress = 1;

                vx_ray3_t ray;
                vx_camera_pos_compute_ray(pos, mouse->x, mouse->y, &ray);

                // Set the manipulation point where the user clicked
                vx_ray3_intersect_xy(&ray, 0.0, deh->manipulation_point);
                return 1;
            } else if (mouse->button_mask == VX_BUTTON3_MASK) { // rotation
                deh->manip_in_progress = 1;

                deh->last_rotate_x = mouse->x;
                deh->last_rotate_y = mouse->y;

                // Unlike the vis implementation, we don't assign a manipulation point for rotation mode
                return 1;
            }
        }
        // PART 2: Mouse Up events (disable all manip)
        if (button_up) {
            deh->manip_in_progress = 0;
            return 1;
        }

        // PART 3: Mouse Dragged/Zoom events
        zarray_t * fp = zarray_create(sizeof(matd_t*));
        matd_t * eye = matd_create_data(3,1, pos->eye);    zarray_add(fp, &eye);
        matd_t * lookat = matd_create_data(3,1, pos->lookat); zarray_add(fp, &lookat);
        matd_t * up = matd_create_data(3,1, pos->up);     zarray_add(fp, &up);

        if (deh->manip_in_progress && mouse->button_mask == VX_BUTTON1_MASK)  {

            matd_t *mv = matd_create(3,1); zarray_add(fp, &mv);
            matd_t * pm = matd_create(4,4); zarray_add(fp, &pm);

            vx_camera_pos_projection_matrix(pos, pm->data);

            window_space_pan_to(deh->manipulation_point, mouse->x, mouse->y,
                                pm, pos->viewport, eye, lookat, up, mv);

            matd_add_inplace(eye, mv);
            matd_add_inplace(lookat, mv);

            default_cam_mgr_lookat(deh->cam_mgr,
                                   eye->data, lookat->data, up->data, 0, deh->ui_animate_ms);

        } else if (deh->manip_in_progress && mouse->button_mask == VX_BUTTON3_MASK) { // rotate the camera
            double dx = mouse->x - deh->last_rotate_x;
            double dy = mouse->y - deh->last_rotate_y;
            double pixelsToRadians = M_PI /  (pos->viewport[2] >  pos->viewport[3] ? pos->viewport[2] : pos->viewport[3]);

            if (mouse->modifiers & VX_CAPS_MASK) { // rotate roll only
                double cx = pos->viewport[0] + pos->viewport[2]/2;
                double cy = pos->viewport[1] + pos->viewport[3]/2;


                matd_t * v1 = matd_create(3,1); zarray_add(fp, &v1);
                v1->data[0] =    mouse->x - cx;
                v1->data[1] = - (mouse->y - cy);
                v1->data[2] = 0;

                matd_t * v0 = matd_create(3,1); zarray_add(fp, &v0);
                v0->data[0] = deh->last_rotate_x - cx;
                v0->data[1] = - (deh->last_rotate_y - cy);
                v0->data[2] = 0;

                matd_t * v1_norm = matd_vec_normalize(v1); zarray_add(fp, &v1_norm);
                matd_t * v0_norm = matd_vec_normalize(v0); zarray_add(fp, &v0_norm);

                if (dx != 0 || dy != 0) {
                    double theta = mod2pi(atan2(v1_norm->data[1], v1_norm->data[0])
                                          - atan2(v0_norm->data[1], v0_norm->data[0]));

                    if (!isnan(theta)) {

                        matd_t * p2eye = matd_subtract(eye, lookat); zarray_add(fp, &p2eye);

                        matd_t * qcum = matd_create(4,1); zarray_add(fp, &qcum);
                        vx_util_angle_axis_to_quat(theta, p2eye->data, qcum->data);

                        // apply rotation
                        default_cam_mgr_rotate(deh->cam_mgr, qcum->data, deh->ui_animate_ms);
                    }
                }
            } else { // any rotate
                matd_t *qx = matd_create(4,1); zarray_add(fp, &qx);
                matd_t *qy = matd_create(4,1); zarray_add(fp, &qy);

                matd_t * p2eye = matd_subtract(eye, lookat); zarray_add(fp, &p2eye);
                matd_t * left = matd_crossproduct(p2eye,up); zarray_add(fp, &left);

                if (default_cam_mgr_get_interface_mode (deh->cam_mgr) <= 2.0) { // for 2.0 interface mode
                    double view_center_x = pos->viewport[0] + pos->viewport[2]/2.0;
                    double view_center_y = pos->viewport[1] + pos->viewport[3]/2.0;
                    double old_rel_x = deh->last_rotate_x - view_center_x;
                    double old_rel_y = deh->last_rotate_y - view_center_y;
                    double rel_x = mouse->x - view_center_x;
                    double rel_y = mouse->y - view_center_y;

                    double angle = atan2(rel_y,rel_x) - atan2(old_rel_y, old_rel_x);
                    matd_t * zaxis = matd_create(3,1); zarray_add(fp, &zaxis);
                    zaxis->data[2] = (eye->data[2] > 0) ? -1 : 1;
                    vx_util_angle_axis_to_quat(angle, zaxis->data, qx->data);
                    vx_util_angle_axis_to_quat(0.0, left->data, qy->data); // 2d mode only rotates around zaxis
                }
                else if (default_cam_mgr_get_interface_mode (deh->cam_mgr) == 2.5) { // for 2.5 interface mode
                    matd_t * up_on_plane = matd_copy(up); zarray_add(fp, &up_on_plane);
                    up_on_plane->data[2] = 0;
                    up_on_plane = matd_vec_normalize (up_on_plane); zarray_add(fp, &up_on_plane);
                    matd_t * p2eye_normalized = matd_vec_normalize(p2eye); zarray_add(fp, &p2eye_normalized);
                    double init_elevation = acos(-matd_vec_dot_product(up_on_plane, p2eye_normalized));

                    if (init_elevation > M_PI*11/16)
                        init_elevation -= M_PI;

                    double delevation = -dy*pixelsToRadians;

                    if (init_elevation + delevation < -M_PI/8) // don't allow elevation to go below -22.5 deg
                        delevation = -M_PI/8 - init_elevation;
                    else if (init_elevation + delevation > M_PI/2) // don't allow elevation to exceed 90 deg
                        delevation = M_PI/2 - init_elevation;

                    matd_t * zaxis = matd_create(3,1); zarray_add(fp, &zaxis);
                    zaxis->data[2] = (up->data[2] > 0) ? 1 : -1;
                    vx_util_angle_axis_to_quat(-dx*pixelsToRadians, zaxis->data, qx->data);
                    vx_util_angle_axis_to_quat(delevation, left->data, qy->data);
                } else { // for 3.0 interface mode
                    vx_util_angle_axis_to_quat(-dx*pixelsToRadians, up->data, qx->data);
                    vx_util_angle_axis_to_quat(-dy*pixelsToRadians, left->data, qy->data);
                }

                matd_t * qcum = matd_create(4,1); zarray_add(fp, &qcum);
                vx_util_quat_multiply(qx->data,qy->data, qcum->data);

                // apply rotation
                default_cam_mgr_rotate(deh->cam_mgr, qcum->data, deh->ui_animate_ms);
            }

            deh->last_rotate_x = mouse->x;
            deh->last_rotate_y = mouse->y;
        }

        // cleanup
        zarray_vmap(fp, matd_destroy);
        zarray_destroy(fp);
    } else {
        deh->manip_in_progress = 0;
    }

    // Can always zoom, regardless of which modifiers are down
    if (mouse->scroll_amt != 0) { // handle zoom, ONLY when no other interaction is occurring!
        zarray_t * fp = zarray_create(sizeof(matd_t*));
        matd_t * eye = matd_create_data(3,1, pos->eye);    zarray_add(fp, &eye);
        matd_t * lookat = matd_create_data(3,1, pos->lookat); zarray_add(fp, &lookat);
        matd_t * up = matd_create_data(3,1, pos->up);     zarray_add(fp, &up);


        double zoom_manip[3]; // Zooming always chooses its own state-less manipulation point
        vx_ray3_t ray;
        vx_camera_pos_compute_ray(pos, mouse->x, mouse->y, &ray);
        vx_ray3_intersect_xy(&ray, 0.0, zoom_manip); // XXX No manipulation manager right now

        double SCROLL_ZOOM_FACTOR = pow(2, shift ? 1 : 0.25);
        // XXX Need a way to get shift double SHIFT_SCROLL_ZOOM_FACTOR = pow(2, 1);

        double factor = SCROLL_ZOOM_FACTOR;
        if (mouse->scroll_amt > 0)
            factor = 1.0 / factor;

        matd_t * eye2ground = matd_subtract(lookat, eye); zarray_add(fp, &eye2ground);
        matd_t * look_dir = matd_vec_normalize(eye2ground); zarray_add(fp, &look_dir);

        double next_dist = matd_vec_dist(eye,lookat)*factor;
        matd_scale_inplace(look_dir, next_dist);

        matd_t * next_eye = matd_subtract(lookat, look_dir); zarray_add(fp, &next_eye);
        memcpy(eye->data, next_eye->data, sizeof(double)*3);

        matd_t *mv = matd_create(3,1); zarray_add(fp, &mv);
        matd_t * pm = matd_create(4,4); zarray_add(fp, &pm);

        vx_camera_pos_projection_matrix(pos, pm->data);

        window_space_pan_to(zoom_manip, mouse->x, mouse->y,
                            pm, pos->viewport, eye, lookat, up, mv);

        matd_add_inplace(eye, mv);
        matd_add_inplace(lookat, mv);

        default_cam_mgr_lookat(deh->cam_mgr,
                               eye->data, lookat->data, up->data,
                               0, deh->ui_animate_ms);

        // cleanup
        zarray_vmap(fp, matd_destroy);
        zarray_destroy(fp);
    }



    return 0;
}

static int default_key_event(vx_event_handler_t * vh, vx_layer_t * vl, vx_key_event_t * event)
{
    default_event_handler_t * deh = vh->impl;

    if (event->released &&
        event->key_code >= VX_KEY_F1 &&
        event->key_code <= VX_KEY_F24 ) {

        if (event->modifiers & VX_CTRL_MASK) {
            // save the current camera position
            vx_camera_pos_t * old_pos = NULL;

            uint64_t mtime = 0;
            vx_camera_pos_t * pos = default_cam_mgr_get_cam_target(deh->cam_mgr, &mtime);

            printf("eye =[%.3lf %.3lf %.3lf]\n",
                   pos->eye[0], pos->eye[1], pos->eye[2]);
            printf("lookat = [%.3lf %.3lf %.3lf]\n",
                   pos->lookat[0], pos->lookat[1], pos->lookat[2]);
            printf("up = [%.3lf %.3lf %.3lf]\n",
                   pos->up[0], pos->up[1], pos->up[2]);

            zhash_put(deh->bookmarks, &event->key_code, &pos, NULL, &old_pos);

            free(old_pos);

        } else {
            // load the camera position

            vx_camera_pos_t * pos = NULL;
            zhash_get(deh->bookmarks, &event->key_code, &pos);

            if (pos == NULL) {
                printf("NFO: No bookmark exists for F%d yet. (Use Ctrl+F%d to set)\n",
                       event->key_code - VX_KEY_F1 + 1,
                       event->key_code - VX_KEY_F1 + 1);
            } else {
                // XXX need to specify animate time (should be much longer than UI)!
                // set_cam_pos preserves ortho/perspective mode, lookat does not
                default_cam_mgr_set_cam_pos(deh->cam_mgr, pos, 0, deh->ui_animate_ms*5);
            }


        }
    }

    return 0;
}

static int default_touch_event(vx_event_handler_t * vh, vx_layer_t * vl, vx_camera_pos_t * pos, vx_touch_event_t * mouse)
{
    return 0;
}

static void default_destroy(vx_event_handler_t * vh)
{
    default_event_handler_t * deh = vh->impl;

    zhash_vmap_values(deh->bookmarks, free); // vx_camera_pos_t *
    zhash_destroy(deh->bookmarks); // vx_camera_pos_t *
    free(vh->impl);
    free(vh);
}

vx_event_handler_t * default_event_handler_create(default_cam_mgr_t * cam_mgr, uint32_t ui_animate_ms)
{
    default_event_handler_t * deh = calloc(1,sizeof(default_event_handler_t));
    deh->cam_mgr = cam_mgr;
    deh->bookmarks = zhash_create(sizeof(uint32_t), sizeof(vx_camera_pos_t *), zhash_uint32_hash, zhash_uint32_equals);
    deh->ui_animate_ms = ui_animate_ms;


    vx_event_handler_t * eh = calloc(1, sizeof(vx_event_handler_t));
    eh->touch_event = default_touch_event;
    eh->mouse_event = default_mouse_event;
    eh->key_event = default_key_event;
    eh->destroy = default_destroy;
    eh->impl = deh;
    eh->dispatch_order = 1000;

    return eh;
}
