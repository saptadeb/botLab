#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

#include "vx_tcp_display.h"
#include "common/ssocket.h"
#include "common/ioutils.h"
#include "vx_resc.h"
#include "vx_code_output_stream.h"
#include "vx_code_input_stream.h"
#include "vx_codes.h"
#include "vx_resc_manager.h"
#include "vx_event.h"
#include "vx_camera_pos.h"
#include "vx_tcp_util.h"
#include "vx_util.h"

#define IMPL_TYPE 0x9fabbe12

static int verbose = 0;

typedef struct
{
    vx_display_t * disp;
    vx_resc_manager_t * mgr;

    int cxn_stopped; // set to 1 on stop

    int max_bandwidth_KBs; // < 0 unlimited, [0,...) limited

    // Lock whenever processing codes or resources from display
    // callbacks. Ensures the "mgr" state is consistent with what is
    // actually transmitted on the wire. Could make the write mutex
    // unnecessary
    pthread_mutex_t state_mutex;

    ssocket_t * cxn;
    pthread_mutex_t write_mutex;
    pthread_mutex_t read_mutex;
    pthread_t read_thread;

    pthread_mutex_t list_mutex; // XXX Should probably make this a recursive mutex?
    zarray_t * listeners; // <vx_display_listener_t*>

    void (*cxn_closed_callback)(vx_display_t * disp, void * cpriv);
    void * cpriv;
} tcp_state_t;

static void write_code_data(tcp_state_t * state, int op_type, const uint8_t * data, int datalen)
{
    if (verbose) printf("Sending code %d len %d\n",op_type, datalen);

    int fd = ssocket_get_fd(state->cxn);

    vx_code_output_stream_t * combined = vx_code_output_stream_create(datalen + 8);
    combined->write_uint32(combined, op_type);
    combined->write_uint32(combined, datalen);
    combined->write_bytes(combined, data, datalen);

    pthread_mutex_lock(&state->write_mutex);
    uint64_t before_mtime = vx_util_mtime();

    write_fully(fd, combined->data, combined->pos);
    uint64_t after_mtime = vx_util_mtime();

    if (state->max_bandwidth_KBs >= 0) {
        // in seconds:
        double dt = (after_mtime - before_mtime) / 1e3;
        double desired_dt = (datalen/1e3) / state->max_bandwidth_KBs;

        int64_t sleep_us = (int64_t)((desired_dt - dt) * 1e6);

        if (verbose > 1) printf("datalen %d dt %f desired_dt %f usleep %ld\n",
                            datalen, dt, desired_dt, sleep_us);

        if (sleep_us > 0) // avoid zero, neg sleeps
            usleep(sleep_us);
    }
    pthread_mutex_unlock(&state->write_mutex);

    vx_code_output_stream_destroy(combined);
}

static void send_codes(vx_display_t * disp, const uint8_t * data, int datalen)
{
    vx_code_input_stream_t * cins = vx_code_input_stream_create(data, datalen);
    // Peek at the code type. Only some codes are forwarded verbatim
    uint32_t code = cins->read_uint32(cins);

    tcp_state_t * state = disp->impl;

    pthread_mutex_lock(&state->state_mutex);
    switch(code) {
        case OP_BUFFER_RESOURCES:
            vx_resc_manager_buffer_resources(state->mgr, data, datalen);
            break;
        case OP_BUFFER_CODES:
        case OP_LAYER_INFO:
        case OP_DEALLOC_RESOURCES:
        default:
            write_code_data((tcp_state_t *)disp->impl, VX_TCP_CODES, data, datalen);
    }
    pthread_mutex_unlock(&state->state_mutex);

    vx_code_input_stream_destroy(cins);
}

static void send_resources(vx_display_t * disp, zhash_t * all_resources)
{
    tcp_state_t * state = disp->impl;

    pthread_mutex_lock(&state->state_mutex);
    // XXX mutex?
    zhash_t * transmit = vx_resc_manager_dedup_resources(state->mgr, all_resources);

    if (verbose) {
        printf("DBG: all resources: ");
        zhash_iterator_t itr;
        zhash_iterator_init(all_resources, &itr);
        uint64_t guid = -1;
        vx_resc_t* vr = NULL;
        while(zhash_iterator_next(&itr, &guid, &vr)) {
            printf("%ld[%d], ", guid, vr->count*vr->fieldwidth);
        }
        printf("\n");
    }

    if (verbose) {
        printf("DBG: Transmit resources: ");
        zhash_iterator_t itr;
        zhash_iterator_init(transmit, &itr);
        uint64_t guid = -1;
        vx_resc_t* vr = NULL;
        while(zhash_iterator_next(&itr, &guid, &vr)) {
            printf("%ld[%d], ", guid, vr->count*vr->fieldwidth);
        }
        printf("\n");
    }

    vx_code_output_stream_t * ocodes = vx_code_output_stream_create(256);

    vx_tcp_util_pack_resources(transmit, ocodes);

    write_code_data((tcp_state_t*) disp->impl, VX_TCP_ADD_RESOURCES, ocodes->data, ocodes->pos);

    pthread_mutex_unlock(&state->state_mutex);

    vx_code_output_stream_destroy(ocodes);
    zhash_destroy(transmit);
}

typedef struct
{
    int code;
    int datalen;
} read_header_t;

static void process_viewport(tcp_state_t * state, uint8_t * data, int datalen)
{
    assert(datalen == 8);
    vx_code_input_stream_t * cins = vx_code_input_stream_create(data, datalen);
    int width = cins->read_uint32(cins);
    int height = cins->read_uint32(cins);
    vx_code_input_stream_destroy(cins);

    pthread_mutex_lock(&state->list_mutex);
    vx_display_listener_t *listener = NULL;
    for (int i = 0; i < zarray_size(state->listeners); i++) {
        zarray_get(state->listeners, i, &listener);
        listener->viewport_changed(listener, width, height);
    }
    pthread_mutex_unlock(&state->list_mutex);
}


static void process_mouse(tcp_state_t * state, uint8_t * data, int datalen)
{
    vx_code_input_stream_t * cins = vx_code_input_stream_create(data, datalen);
    int layer_id = cins->read_uint32(cins);

    vx_camera_pos_t pos;
    vx_tcp_util_unpack_camera_pos(cins, &pos);

    vx_mouse_event_t mev;
    mev.x = cins->read_float(cins);
    mev.y = cins->read_float(cins);

    mev.button_mask = cins->read_uint32(cins);
    mev.scroll_amt = (int32_t)cins->read_uint32(cins); // cast back to signed
    mev.modifiers = cins->read_uint32(cins);

    pthread_mutex_lock(&state->list_mutex);
    vx_display_listener_t *listener = NULL;
    for (int i = 0; i < zarray_size(state->listeners); i++) {
        zarray_get(state->listeners, i, &listener);
        listener->event_dispatch_mouse(listener, layer_id, &pos, &mev);
    }
    pthread_mutex_unlock(&state->list_mutex);

    assert(vx_code_input_stream_available(cins) == 0);
    vx_code_input_stream_destroy(cins);
}


static void process_key(tcp_state_t * state, uint8_t * data, int datalen)
{
    vx_code_input_stream_t * cins = vx_code_input_stream_create(data, datalen);
    int layer_id = cins->read_uint32(cins);

    vx_key_event_t kev;
    kev.modifiers = cins->read_uint32(cins);
    kev.key_code  = cins->read_uint32(cins);
    kev.released  = cins->read_uint32(cins);

    pthread_mutex_lock(&state->list_mutex);
    vx_display_listener_t *listener = NULL;
    for (int i = 0; i < zarray_size(state->listeners); i++) {
        zarray_get(state->listeners, i, &listener);
        listener->event_dispatch_key(listener, layer_id, &kev);
    }
    pthread_mutex_unlock(&state->list_mutex);

    assert(vx_code_input_stream_available(cins) == 0);
    vx_code_input_stream_destroy(cins);
}

static void process_touch(tcp_state_t * state, uint8_t * data, int datalen)
{
    vx_code_input_stream_t * cins = vx_code_input_stream_create(data, datalen);
    int layer_id = cins->read_uint32(cins);

    vx_camera_pos_t pos;
    vx_tcp_util_unpack_camera_pos(cins, &pos);

    vx_touch_event_t touch;
    touch.finger_count = cins->read_uint32(cins);
    touch.x = calloc(touch.finger_count, sizeof(float));
    touch.y = calloc(touch.finger_count, sizeof(float));
    touch.ids = calloc(touch.finger_count, sizeof(int));

    for (int i = 0; i < touch.finger_count; i++) {
        touch.x[i] = cins->read_float(cins);
        touch.y[i] = cins->read_float(cins);
        touch.ids[i] = cins->read_uint32(cins);
    }
    touch.action_id = cins->read_uint32(cins);
    touch.action = cins->read_uint32(cins);


    pthread_mutex_lock(&state->list_mutex);
    vx_display_listener_t *listener = NULL;
    for (int i = 0; i < zarray_size(state->listeners); i++) {
        zarray_get(state->listeners, i, &listener);
        listener->event_dispatch_touch(listener, layer_id, &pos, &touch);
    }
    pthread_mutex_unlock(&state->list_mutex);

    assert(vx_code_input_stream_available(cins) == 0);
    vx_code_input_stream_destroy(cins);
}

static void process_camera(tcp_state_t * state, uint8_t * data, int datalen)
{
    vx_code_input_stream_t * cins = vx_code_input_stream_create(data, datalen);
    int layer_id = cins->read_uint32(cins);

    vx_camera_pos_t pos;
    vx_tcp_util_unpack_camera_pos(cins, &pos);


    pthread_mutex_lock(&state->list_mutex);
    vx_display_listener_t *listener = NULL;
    for (int i = 0; i < zarray_size(state->listeners); i++) {
        zarray_get(state->listeners, i, &listener);
        listener->camera_changed(listener, layer_id, &pos);
    }
    pthread_mutex_unlock(&state->list_mutex);

    assert(vx_code_input_stream_available(cins) == 0);
    vx_code_input_stream_destroy(cins);
}

static void * read_run(void * ptr)
{
    assert(sizeof(read_header_t) == 8); // enforce struct alignment the way we expect

    tcp_state_t * state = ptr;

    if (verbose) printf("Read thread starting\n");
    while(1) {
        read_header_t header;
        pthread_mutex_lock(&state->read_mutex);
        int res_cnt = read_fully(ssocket_get_fd(state->cxn), &header, sizeof(read_header_t));
        pthread_mutex_unlock(&state->read_mutex);
        if (res_cnt < 0) { // error or closed
            break;
        }

        header.code = be32toh(header.code);
        header.datalen = be32toh(header.datalen);

        uint8_t data[header.datalen];

        pthread_mutex_lock(&state->read_mutex);
        res_cnt = read_fully(ssocket_get_fd(state->cxn), data, header.datalen);
        pthread_mutex_unlock(&state->read_mutex);
        if (res_cnt < 0) { // error or closed
            break;
        }

        //printf("Debug code %d 0x%x datalen %d\n",header.code, header.code, header.datalen);
        switch(header.code) {
            case VX_TCP_VIEWPORT_SIZE:
                process_viewport(state, data, header.datalen);
                break;
            case VX_TCP_EVENT_TOUCH:
                process_touch(state, data, header.datalen);
                break;
            case VX_TCP_EVENT_MOUSE:
                process_mouse(state, data, header.datalen);
                break;
            case VX_TCP_EVENT_KEY:
                process_key(state, data, header.datalen);
                break;

            case VX_TCP_CAMERA_CHANGED:
                process_camera(state, data, header.datalen);
                break;

            default:
                printf("Uknown TCP code %d 0x%x datalen %d\n",header.code, header.code, header.datalen);
        }
    }

    if (verbose) printf("Connection closed!\n");

    state->cxn_closed_callback(state->disp, state->cpriv);

    pthread_exit(NULL);
}

static void state_destroy(tcp_state_t * state)
{
    if (!state->cxn_stopped)
        ssocket_destroy(state->cxn);

    //XXX is this necessary if we've already closed the socket??
    // pthread_cancel(state->read_thread);
    pthread_join(state->read_thread, NULL);

    pthread_mutex_destroy(&state->write_mutex);
    pthread_mutex_destroy(&state->read_mutex);

    pthread_mutex_destroy(&state->state_mutex);

    pthread_mutex_destroy(&state->list_mutex);

    zarray_destroy(state->listeners);
    vx_resc_manager_destroy(state->mgr);

    printf("Destroy state 0x%p\n",(void*)state);

    free(state);
}

static void add_listener(vx_display_t * disp, vx_display_listener_t * listener)
{
    tcp_state_t * state = disp->impl;
    pthread_mutex_lock(&state->list_mutex);
    zarray_add(state->listeners, &listener);
    if (verbose) printf("Now have %d listeners. Added 0x%p\n", zarray_size(state->listeners), (void*) listener);
    pthread_mutex_unlock(&state->list_mutex);


}

static void remove_listener(vx_display_t * disp, vx_display_listener_t * listener)
{
    tcp_state_t * state = disp->impl;
    pthread_mutex_lock(&state->list_mutex);
    zarray_remove_value(state->listeners, &listener, 0);
    pthread_mutex_unlock(&state->list_mutex);
}

void vx_tcp_display_destroy(vx_display_t * disp)
{
    tcp_state_t * state = disp->impl;

    state_destroy(state);
    free(disp);
}

void vx_tcp_display_close(vx_display_t * disp)
{
    tcp_state_t * state = disp->impl;
    if (verbose) printf("NFO: Graceful closing of tcp display 0x%p\n", (void*)disp);
    // XXX This is a bit hacky, due to the threading, and endless blocking
    state->cxn_stopped = 1;
    ssocket_destroy(state->cxn);

}

static tcp_state_t * state_create(ssocket_t * cxn, void (*cxn_closed_callback)(vx_display_t * disp, void * cpriv), void * cpriv, vx_display_t * disp)
{
    tcp_state_t * state =  calloc(1, sizeof(tcp_state_t));
    state->disp = disp;

    state->cxn = cxn;
    state->cxn_closed_callback = cxn_closed_callback;
    state->cpriv = cpriv;


    state->mgr = vx_resc_manager_create(disp);

    pthread_mutex_init(&state->list_mutex, NULL);
    state->listeners = zarray_create(sizeof(vx_display_listener_t*));

    pthread_mutex_init(&state->write_mutex, NULL);
    pthread_mutex_init(&state->read_mutex, NULL);


    pthread_mutexattr_t mutexAttr;
    pthread_mutexattr_init(&mutexAttr);
    pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_RECURSIVE);

    pthread_mutex_init(&state->state_mutex, &mutexAttr);
    pthread_create(&state->read_thread,NULL, read_run, state);

    return state;
}

vx_display_t * vx_tcp_display_create(ssocket_t * cxn, int limitKBs, void (*cxn_closed_callback)(vx_display_t * disp, void * cpriv), void * cpriv)
{
    vx_display_t * disp = calloc(1, sizeof(vx_display_t));
    disp->impl_type = IMPL_TYPE;
    tcp_state_t * state = state_create(cxn, cxn_closed_callback, cpriv, disp);
    disp->impl = state;
    state->max_bandwidth_KBs = limitKBs;

    disp->send_codes = send_codes;
    disp->send_resources = send_resources;
    disp->add_listener = add_listener;
    disp->remove_listener = remove_listener;

    return disp;
}
