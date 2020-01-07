#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#include "vx_layer.h"
#include "vx_colors.h"

#include "vx_codes.h"
#include "default_camera_mgr.h"
#include "default_event_handler.h"

#define UI_ANIMATE_MS 100

struct vx_layer
{
    int layer_id;
    vx_world_t * world;
    int draw_order;
    float bg_color[4];

    vx_display_listener_t display_listener;
    vx_world_listener_t world_listener;

    pthread_mutex_t handler_mutex; // for both event and cam
    zarray_t * event_handlers;
    zarray_t * camera_listeners;

    vx_display_t * disp;

};

static int atomicLayerID = 1;

static void vx_event_handler_destroy(vx_event_handler_t * eh)
{
    eh->destroy(eh);
}

static void vx_camera_listener_destroy(vx_camera_listener_t * cl)
{
    cl->destroy(cl);
}

int vx_layer_id(vx_layer_t * layer)
{
    return layer->layer_id;
}

// XXX We could potentially cache these settings so we can push them onto a new display when it
// connects
void vx_layer_buffer_enabled(vx_layer_t * vl, const char * buffer_name, int enabled)
{
    vx_code_output_stream_t * outs = vx_code_output_stream_create(128);
    outs->write_uint32 (outs, OP_BUFFER_ENABLED);
    outs->write_uint32 (outs, vl->layer_id);
    outs->write_str (outs, buffer_name);
    outs->write_uint8 (outs, enabled? 1 : 0); // convert to byte
    vl->disp->send_codes (vl->disp, outs->data, outs->pos);
    vx_code_output_stream_destroy (outs);
}

static void send_layer_info_codes(vx_layer_t * vl)
{
    if (vl->disp == NULL)
        return;

    vx_code_output_stream_t * couts = vx_code_output_stream_create(128);
    couts->write_uint32(couts, OP_LAYER_INFO);
    couts->write_uint32(couts, vl->layer_id);
    couts->write_uint32(couts, vx_world_get_id(vl->world));
    couts->write_uint32(couts, vl->draw_order);
    couts->write_float(couts, vl->bg_color[0]);
    couts->write_float(couts, vl->bg_color[1]);
    couts->write_float(couts, vl->bg_color[2]);
    couts->write_float(couts, vl->bg_color[3]);

    vl->disp->send_codes(vl->disp, couts->data, couts->pos);

    vx_code_output_stream_destroy(couts);
}

static void viewport_changed(vx_display_listener_t * listener, int width, int height)
{
    // ignore this for now
}

static void camera_changed(vx_display_listener_t * listener, int layer_id, vx_camera_pos_t * pos)
{
    vx_layer_t * vl = listener->impl;
    if (vl->layer_id != layer_id)
        return;

    // ignore this for now
    pthread_mutex_lock(&vl->handler_mutex);
    for (int i = 0; i < zarray_size(vl->camera_listeners); i++) {
        vx_camera_listener_t * cl = NULL;
        zarray_get(vl->camera_listeners, i, &cl);

        cl->camera_changed(cl, vl, pos);
    }

    pthread_mutex_unlock(&vl->handler_mutex);

}

static void event_dispatch_touch(vx_display_listener_t * listener, int layer_id, vx_camera_pos_t * pos, vx_touch_event_t * touch)
{
    vx_layer_t * vl = listener->impl;
    if (vl->layer_id != layer_id)
        return;

    pthread_mutex_lock(&vl->handler_mutex);
    for (int i = 0; i < zarray_size(vl->event_handlers); i++) {
        vx_event_handler_t * handler = NULL;
        zarray_get(vl->event_handlers, i, &handler);
        int handled = handler->touch_event(handler, vl, pos, touch);
        if (handled) {
            break;
        }
    }
    pthread_mutex_unlock(&vl->handler_mutex);

}

static void event_dispatch_mouse(vx_display_listener_t * listener, int layer_id, vx_camera_pos_t * pos, vx_mouse_event_t * mouse)
{
    vx_layer_t * vl = listener->impl;
    if (vl->layer_id != layer_id)
        return;

    pthread_mutex_lock(&vl->handler_mutex);
    for (int i = 0; i < zarray_size(vl->event_handlers); i++) {
        vx_event_handler_t * handler = NULL;
        zarray_get(vl->event_handlers, i, &handler);
        int handled = handler->mouse_event(handler, vl, pos, mouse);
        if (handled) {
            break;
        }
    }
    pthread_mutex_unlock(&vl->handler_mutex);
    return;
}

static void event_dispatch_key(vx_display_listener_t * listener, int layer_id, vx_key_event_t * key)
{
    vx_layer_t * vl = listener->impl;
    if (vl->layer_id != layer_id)
        return;

    pthread_mutex_lock(&vl->handler_mutex);
    for (int i = 0; i < zarray_size(vl->event_handlers); i++) {
        vx_event_handler_t * handler = NULL;
        zarray_get(vl->event_handlers, i, &handler);
        int handled = handler->key_event(handler, vl, key);
        if (handled) {
            break;
        }
    }
    pthread_mutex_unlock(&vl->handler_mutex);
    return;
}

static void send_codes(vx_world_listener_t * listener, const uint8_t * data, int datalen)
{
    vx_layer_t * vl = listener->impl;
    assert(vl->disp != NULL);
    vl->disp->send_codes(vl->disp, data, datalen);

}

static void send_resources(vx_world_listener_t * listener, zhash_t * resources)
{
    vx_layer_t * vl = listener->impl;
    assert(vl->disp != NULL);
    vl->disp->send_resources(vl->disp, resources);
}


vx_layer_t * vx_layer_create(vx_world_t * world)
{

    vx_layer_t * vl = calloc(1,sizeof(vx_layer_t));
    vl->layer_id = __sync_fetch_and_add(&atomicLayerID, 1);
    vl->world = world;
    vl->draw_order = 0;
    memcpy(vl->bg_color, vx_white, sizeof(float)*4);

    { // world listener
        vl->world_listener.impl = vl;
        vl->world_listener.send_codes = send_codes;
        vl->world_listener.send_resources = send_resources;
    }

    { // Display listener
        vl->display_listener.impl = vl;
        vl->display_listener.viewport_changed = viewport_changed;
        vl->display_listener.event_dispatch_key = event_dispatch_key;
        vl->display_listener.event_dispatch_mouse = event_dispatch_mouse;
        vl->display_listener.event_dispatch_touch = event_dispatch_touch;
        vl->display_listener.camera_changed = camera_changed;
    }

    pthread_mutex_init(&vl->handler_mutex, NULL);
    vl->event_handlers = zarray_create(sizeof(vx_event_handler_t*));
    vl->camera_listeners = zarray_create(sizeof(vx_camera_listener_t*));

    vl->disp = NULL; // wait for callback

    return vl;
}

void vx_layer_destroy(vx_layer_t * layer)
{
    vx_layer_set_display(layer, NULL); // ensure that the display is disconnected?

    pthread_mutex_destroy(&layer->handler_mutex);
    zarray_vmap(layer->event_handlers, vx_event_handler_destroy);
    zarray_destroy(layer->event_handlers);

    zarray_vmap(layer->camera_listeners, vx_camera_listener_destroy);
    zarray_destroy(layer->camera_listeners);
    free(layer);
}

// It's important that we only register as a listener to the world
// when we have a valid display connected. Otherwise we create a race condition
// where the world could send only 1/3 co-dependent commands to the
// newly connected display
void vx_layer_set_display(vx_layer_t * vl, vx_display_t * disp)
{
    // dealloc
    if (disp == NULL && vl->disp != NULL) {
        vx_world_remove_listener(vl->world, &vl->world_listener);
        vl->disp->remove_listener(vl->disp, &vl->display_listener);
        vl->disp = NULL;
    }

    // XXX Handle case where we are replacing an old display

    // alloc
    if (disp != NULL) {
        vl->disp = disp;

        vl->disp->add_listener(vl->disp, &vl->display_listener);
        send_layer_info_codes(vl);

        vx_world_add_listener(vl->world, &vl->world_listener);
    }
}

void vx_layer_set_viewport_abs(vx_layer_t * vl, int opcode, int width, int height)
{
    vx_code_output_stream_t * couts = vx_code_output_stream_create(128);
    couts->write_uint32(couts, OP_LAYER_VIEWPORT_ABS);
    couts->write_uint32(couts, vl->layer_id);
    couts->write_uint32(couts, opcode);

    // currently don't expose either the offx, offy, or the animate time
    couts->write_uint32(couts, 0);
    couts->write_uint32(couts, 0);

    couts->write_uint32(couts, width);
    couts->write_uint32(couts, height);

    couts->write_uint32(couts, 0); //UI_ANIMATE_MS

    vl->disp->send_codes(vl->disp, couts->data, couts->pos);

    vx_code_output_stream_destroy(couts);
}

void vx_layer_set_viewport_rel(vx_layer_t * vl, float viewport[4])
{
    vx_code_output_stream_t * couts = vx_code_output_stream_create(128);
    couts->write_uint32(couts, OP_LAYER_VIEWPORT_REL);
    couts->write_uint32(couts, vl->layer_id);
    for (int i = 0; i < 4; i++)
        couts->write_float(couts, viewport[i]);
    couts->write_uint32(couts, 0); //UI_ANIMATE_MS

    vl->disp->send_codes(vl->disp, couts->data, couts->pos);

    vx_code_output_stream_destroy(couts);
}

void vx_layer_camera_op(vx_layer_t *vl, int op_code)
{
    vx_code_output_stream_t * couts = vx_code_output_stream_create(128);
    couts->write_uint32(couts, OP_LAYER_CAMERA);
    couts->write_uint32(couts, vl->layer_id);
    couts->write_uint32(couts, op_code);
    vl->disp->send_codes(vl->disp, couts->data, couts->pos);

    vx_code_output_stream_destroy(couts);
}

void vx_layer_camera_lookat_timed (vx_layer_t *vl,
                                   const float *eye3, const float *lookat3, const float *up3,
                                   uint8_t set_default, uint64_t animate_ms) {


    assert(vl->disp != NULL && "Must set a display before setting camera");

    vx_code_output_stream_t * couts = vx_code_output_stream_create(128);
    couts->write_uint32(couts, OP_LAYER_CAMERA);
    couts->write_uint32(couts, vl->layer_id);
    couts->write_uint32(couts, OP_LOOKAT);

    couts->write_float(couts, eye3[0]);
    couts->write_float(couts, eye3[1]);
    couts->write_float(couts, eye3[2]);

    couts->write_float(couts, lookat3[0]);
    couts->write_float(couts, lookat3[1]);
    couts->write_float(couts, lookat3[2]);

    couts->write_float(couts, up3[0]);
    couts->write_float(couts, up3[1]);
    couts->write_float(couts, up3[2]);

    couts->write_uint32(couts, animate_ms);
    couts->write_uint8(couts, set_default);

    vl->disp->send_codes(vl->disp, couts->data, couts->pos);

    vx_code_output_stream_destroy(couts);
}

void vx_layer_camera_lookat(vx_layer_t * vl, const float * eye3, const float* lookat3, const float * up3,
                            uint8_t set_default)
{
    vx_layer_camera_lookat_timed (vl, eye3, lookat3, up3, set_default, UI_ANIMATE_MS);
}

void vx_layer_camera_fit2D(vx_layer_t * vl, const float * xy0,
                           const float* xy1, uint8_t set_default)
{
    assert(vl->disp != NULL && "Must set a display before setting camera");

    vx_code_output_stream_t * couts = vx_code_output_stream_create(128);
    couts->write_uint32(couts, OP_LAYER_CAMERA);
    couts->write_uint32(couts, vl->layer_id);
    couts->write_uint32(couts, OP_FIT2D);

    couts->write_float(couts, xy0[0]);
    couts->write_float(couts, xy0[1]);

    couts->write_float(couts, xy1[0]);
    couts->write_float(couts, xy1[1]);

    couts->write_uint32(couts, UI_ANIMATE_MS);
    couts->write_uint8(couts, set_default);

    vl->disp->send_codes(vl->disp, couts->data, couts->pos);

    vx_code_output_stream_destroy(couts);
}

void vx_layer_camera_follow_disable(vx_layer_t * vl)
{
    assert(vl->disp != NULL && "Must set a display before setting camera");

    vx_code_output_stream_t * couts = vx_code_output_stream_create(128);
    couts->write_uint32(couts, OP_LAYER_CAMERA);
    couts->write_uint32(couts, vl->layer_id);
    couts->write_uint32(couts, OP_FOLLOW_MODE_DISABLE);

    vl->disp->send_codes(vl->disp, couts->data, couts->pos);

    vx_code_output_stream_destroy(couts);

}
void vx_layer_camera_follow(vx_layer_t * vl, const double * robot_pos3,
                            const double * robot_quat4, int followYaw)
{
    assert(vl->disp != NULL && "Must set a display before setting camera");

    vx_code_output_stream_t * couts = vx_code_output_stream_create(128);
    couts->write_uint32(couts, OP_LAYER_CAMERA);
    couts->write_uint32(couts, vl->layer_id);

    couts->write_uint32(couts, OP_FOLLOW_MODE);

    couts->write_double(couts, robot_pos3[0]);
    couts->write_double(couts, robot_pos3[1]);
    couts->write_double(couts, robot_pos3[2]);

    couts->write_double(couts, robot_quat4[0]);
    couts->write_double(couts, robot_quat4[1]);
    couts->write_double(couts, robot_quat4[2]);
    couts->write_double(couts, robot_quat4[3]);

    couts->write_uint32(couts, followYaw);
    // Theoretically, should allow people to decide this quantity?
    couts->write_uint32(couts, UI_ANIMATE_MS);

    vl->disp->send_codes(vl->disp, couts->data, couts->pos);

    vx_code_output_stream_destroy(couts);
}


void vx_layer_set_draw_order(vx_layer_t * vl, int draw_order)
{
    vl->draw_order = draw_order;

    send_layer_info_codes(vl);
}

void vx_layer_set_background_color (vx_layer_t * vl, const float * color4)
{
    memcpy(vl->bg_color, color4, sizeof(float)*4);
    send_layer_info_codes(vl);
}

static int zvx_event_handler_compare (const void *a_ptr_ptr, const void *b_ptr_ptr)
{
    vx_event_handler_t *eh1 = *(void **)a_ptr_ptr;
    vx_event_handler_t *eh2 = *(void **)b_ptr_ptr;
    if (eh1->dispatch_order > eh2->dispatch_order) {
        return 1;
    } else if (eh1->dispatch_order < eh2->dispatch_order) {
        return -1;
    }
    return 0;
}


void vx_layer_add_event_handler(vx_layer_t * vl, vx_event_handler_t * eh)
{
    pthread_mutex_lock(&vl->handler_mutex);
    {
        zarray_add(vl->event_handlers, &eh);
        zarray_sort(vl->event_handlers, zvx_event_handler_compare);
    }
    pthread_mutex_unlock(&vl->handler_mutex);
}


void vx_layer_add_camera_listener(vx_layer_t * vl, vx_camera_listener_t * cl)
{
    pthread_mutex_lock(&vl->handler_mutex);
    {
        zarray_add(vl->camera_listeners, &cl);
    }
    pthread_mutex_unlock(&vl->handler_mutex);

}

