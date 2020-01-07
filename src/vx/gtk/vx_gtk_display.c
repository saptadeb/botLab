#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include <gdk/gdkkeysyms.h>

#include "vx/vx_event_handler.h"
#include "vx/vx_camera_pos.h"
#include "vx_gtk_display.h"
#include "vx/vx_gl_renderer.h"
#include "vx/vx_resc_manager.h"
#include "vx/vx_codes.h"
#include "vx/vx_global.h"
#include "vx/glcontext.h"
#include "vx/vx_util.h"
#include "vx/default_camera_mgr.h"
#include "vx/default_event_handler.h"
#include "vx/vx_tcp_util.h"
#include "vx/vx_viewport_mgr.h"
#include "gtkuimagepane.h"
#include "vx_gtk_buffer_manager.h"

#include "vx/math/matd.h"
#include "common/task_thread.h"
#include "common/string_util.h"

#define MIN_GL_MAJOR_VERSION 2
#define VX_GTK_DISPLAY_IMPL 0x1234aaff

// XXX This constant shows up in a couple places
#define UI_ANIMATE_MS 100

typedef struct
{
    int viewport[4]; // [0,0,w,h]

    zarray_t * layers; // <layer_info_t*> layers ordered by draw_order
    zhash_t * camera_positions; //<int, vx_camera_pos_t>
    // XXX The layer positions are redundant, can be accessed via cam pos
    zhash_t * layer_positions; //<int, int *> // viewports

} render_info_t;

#define DISPATCH_TYPE_MOUSE_EVENT 0
#define DISPATCH_TYPE_KEY_EVENT   1
#define DISPATCH_TYPE_TOUCH_EVENT 2
#define DISPATCH_TYPE_CAMERA      3

typedef struct
{
    int type;
    int layer_id;
    // Data
    vx_camera_pos_t * pos; // could be null
    void * data; // mouse event, etc depends on type
} dispatch_event_t;

typedef struct
{
    vx_display_t * super;

    vx_gtk_buffer_manager_t * buffer_manager;
    GtkuImagePane * imagePane; // we should make this be passed into the constructor

    // double buffer of pix buffers
    int cur_pb_idx;
    GdkPixbuf * pixbufs[2]; // stores metadata about malloc'd pixel buffers
    uint8_t * pixdatas[2];

    render_info_t * last_render_info;

    vx_mouse_event_t * last_mouse_event; // stores the last mouse event, used for click detection
    int mouse_pressed_layer_id;

    vx_gl_renderer_t * glrend;
    vx_resc_manager_t * mgr;

    zarray_t * listeners; // <vx_display_listener_t*>
    pthread_mutex_t listener_mutex;

    zhash_t * layer_info_map; // < layerID, layer_info_t>


    pthread_mutex_t mutex; // lock 1) all display calls, 2) all calls to glrend

    pthread_t render_thread;
    int rendering;
    float target_frame_rate;

    // Event handling
    pthread_t dispatch_thread;
    pthread_mutex_t dispatch_mutex;
    pthread_cond_t dispatch_cond;
    zarray_t * dispatch_queue; // <dispatch_event_t*>

    gl_fbo_t * fbo;
    int fbo_width, fbo_height;

    int popup_layer_id;

    // For signaling a new frame is ready
    pthread_mutex_t movie_mutex;
    pthread_cond_t movie_cond;

    pthread_t movie_thread;

    zarray_t * movie_pending; // <movie_frame_t*>
    char * movie_file;

} state_t;

// Contains all details necessary to render:
typedef struct
{
    state_t * state;
    int width, height;
    uint8_t * out_buf;
    int format;

    int rendered; // if this is false, then didn't render
} render_buffer_t;


typedef struct
{
    uint64_t mtime;
    int stride;
    int width, height;
    uint8_t * buf;

} movie_frame_t;

typedef struct
{
    int layer_id;

    /*
    // default layer setup is rel/fullscreen
    int viewport_anchor;// = Vx.OP_LAYER_REL;
    float viewport_rel[4];
    int anchor_width;
    int anchor_height; // for all other anchor types
    int anchor_offx;
    int anchor_offy; // offset from nominal anchor

    // animation of layer changes:
    long mtime0;
    long mtime1;
    float viewport_rel0[4]; // store the position at mtime0;
    // target for mtime1 is above
    */


    int draw_order;

    default_cam_mgr_t * cam_mgr;
    vx_event_handler_t * event_handler;
    vx_viewport_mgr_t * vp_mgr;

} layer_info_t;

static pthread_mutex_t gl_init_mutex = PTHREAD_MUTEX_INITIALIZER;
static task_thread_t * gl_thread = NULL;
static glcontext_t * glc = NULL;

static int verbose = 0;

static int check_viewport_bounds(double  x, double y, int * viewport)
{
    return (x >= viewport[0] &&
            y >= viewport[1] &&
            x < viewport[0] + viewport[2] &&
            y < viewport[1] + viewport[3]);
}

// returns the layer under the specified x,y. Must be externally synchronized
static int pick_layer(render_info_t * rinfo, float x, float y)
{

    int layer_under_mouse_id = 0;
    for (int i = 0; i < zarray_size(rinfo->layers); i++) {
        layer_info_t * linfo = NULL;
        zarray_get(rinfo->layers, i, &linfo);
        int * viewport = NULL;
        zhash_get(rinfo->layer_positions, &linfo->layer_id, &viewport);
        if (check_viewport_bounds(x, y, viewport))
            layer_under_mouse_id = linfo->layer_id; // highest ranked layer that matches will be it
    }
    return layer_under_mouse_id;
}


static void dispatch_event_destroy(dispatch_event_t * dispatchable)
{
    assert(dispatchable->type != DISPATCH_TYPE_TOUCH_EVENT);
    free(dispatchable->pos);
    free(dispatchable->data); // mouse, key are flat structs so we can
                              // free directly
    free(dispatchable);
}

static void dispatch_event(state_t * state, dispatch_event_t * event)
{
    pthread_mutex_lock(&state->dispatch_mutex);
    zarray_add(state->dispatch_queue, &event);
    pthread_cond_signal(&state->dispatch_cond);
    pthread_mutex_unlock(&state->dispatch_mutex);
}


static void menu_action(GtkWidget * menuitem, char * shortname, gpointer userdata)
{
    state_t * state = userdata;
    if (state->popup_layer_id == 0)
        return;

    vx_code_output_stream_t * couts = vx_code_output_stream_create(128);


    if (verbose) printf("DBG menu: Label %s shortname %s\n",
                        gtk_menu_item_get_label(GTK_MENU_ITEM(menuitem)), shortname);

    if (!strcmp(shortname,"reset_camera")) {
        couts->write_uint32(couts, OP_LAYER_CAMERA);
        couts->write_uint32(couts, state->popup_layer_id);
        couts->write_uint32(couts, OP_DEFAULTS);
    } else if (!strcmp(shortname,"ortho")) {
        couts->write_uint32(couts, OP_LAYER_CAMERA);
        couts->write_uint32(couts, state->popup_layer_id);
        couts->write_uint32(couts, OP_PROJ_ORTHO);
    } else if (!strcmp(shortname,"perspective")) {
        couts->write_uint32(couts, OP_LAYER_CAMERA);
        couts->write_uint32(couts, state->popup_layer_id);
        couts->write_uint32(couts, OP_PROJ_PERSPECTIVE);
    } else if (!strcmp(shortname,"iface2.0D")) {
        couts->write_uint32(couts, OP_LAYER_CAMERA);
        couts->write_uint32(couts, state->popup_layer_id);
        couts->write_uint32(couts, OP_INTERFACE_MODE);
        couts->write_float(couts, 2.0f);
    } else if (!strcmp(shortname,"iface2.5D")) {
        couts->write_uint32(couts, OP_LAYER_CAMERA);
        couts->write_uint32(couts, state->popup_layer_id);
        couts->write_uint32(couts, OP_INTERFACE_MODE);
        couts->write_float(couts, 2.5f);
    } else if (!strcmp(shortname,"iface3.0D")) {
        couts->write_uint32(couts, OP_LAYER_CAMERA);
        couts->write_uint32(couts, state->popup_layer_id);
        couts->write_uint32(couts, OP_INTERFACE_MODE);
        couts->write_float(couts, 3.0f);
    } else if (!strcmp(shortname,"buffer_manager")) {
        vx_gtk_buffer_manager_show(state->buffer_manager, 1);
    } else if (!strcmp(shortname,"scene")) {
        couts->write_uint32(couts, OP_WINDOW_SCENE);
        // optional, filled in automatically couts->write_str(couts, "test.vxs");
    } else if (!strcmp(shortname,"screen_shot")) {
        couts->write_uint32(couts, OP_WINDOW_SCREENSHOT);
        // optional, filled in automatically couts->write_str(couts, "test.png");
    } else if (!strcmp(shortname,"record_movie")) {
        couts->write_uint32(couts, OP_WINDOW_MOVIE_RECORD);
        // optional, filled in automatically couts->write_str(couts, "test.ppms.gz");
    } else if (!strcmp(shortname,"stop_movie")) {
        couts->write_uint32(couts, OP_WINDOW_MOVIE_STOP);
    }


    if (couts->pos != 0) {
        state->super->send_codes(state->super, couts->data, couts->pos);
    }

        /* pthread_mutex_lock(&state->mutex); */

        /* pthread_mutex_unlock(&state->mutex); */

    vx_code_output_stream_destroy(couts);
}

static void menu_action_reset_camera(GtkWidget * menuitem, gpointer userdata)
{
    menu_action(menuitem, "reset_camera", userdata);
}

static void menu_action_ortho(GtkWidget * menuitem, gpointer userdata)
{
    menu_action(menuitem, "ortho", userdata);
}

static void menu_action_iface_20d(GtkWidget * menuitem, gpointer userdata)
{
    menu_action(menuitem, "iface2.0D", userdata);
}

static void menu_action_iface_25d(GtkWidget * menuitem, gpointer userdata)
{
    menu_action(menuitem, "iface2.5D", userdata);
}

static void menu_action_iface_30d(GtkWidget * menuitem, gpointer userdata)
{
    menu_action(menuitem, "iface3.0D", userdata);
}

static void menu_action_perspective(GtkWidget * menuitem, gpointer userdata)
{
    menu_action(menuitem, "perspective", userdata);
}

static void menu_action_buffer_manager(GtkWidget * menuitem, gpointer userdata)
{
    menu_action(menuitem, "buffer_manager", userdata);
}

static void menu_action_screen_shot(GtkWidget * menuitem, gpointer userdata)
{
    menu_action(menuitem, "screen_shot", userdata);
}

static void menu_action_scene(GtkWidget * menuitem, gpointer userdata)
{
    menu_action(menuitem, "scene", userdata);
}

static void menu_action_record_movie(GtkWidget * menuitem, gpointer userdata)
{
    menu_action(menuitem, "record_movie", userdata);
}

static void menu_action_stop_movie(GtkWidget * menuitem, gpointer userdata)
{
    menu_action(menuitem, "stop_movie", userdata);
}

void vx_gtk_display_show_context_menu(vx_display_t * disp, vx_mouse_event_t * event)
{
    state_t * state = disp->impl;
    vx_camera_pos_t layer_cam_pos;

    pthread_mutex_lock(&state->mutex);
    state->popup_layer_id = pick_layer(state->last_render_info, event->x, event->y);
    vx_camera_pos_t * pos = NULL;
    zhash_get(state->last_render_info->camera_positions, &state->popup_layer_id, &pos);
    if (pos != NULL)
        memcpy(&layer_cam_pos, pos, sizeof(vx_camera_pos_t));
    pthread_mutex_unlock(&state->mutex);

    if (state->popup_layer_id == 0)
        return;

    layer_info_t * linfo = NULL;
    zhash_get(state->layer_info_map, &state->popup_layer_id, &linfo);


    GtkWidget * menu = gtk_menu_new();

    // Camera buttons
    {
        GtkWidget * labelwrapper = gtk_menu_item_new();
        GtkWidget * label = gtk_label_new("Foo");
        gtk_label_set_markup(GTK_LABEL(label), "<b>Camera options</b>");
        gtk_container_add(GTK_CONTAINER(labelwrapper), label); // Auto centers? I guess that's fine
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), labelwrapper);

        GtkWidget * resetCam = gtk_menu_item_new_with_label("Reset Camera");
        g_signal_connect(resetCam, "activate", (GCallback) menu_action_reset_camera, state);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), resetCam);

        GtkWidget * button1 = gtk_check_menu_item_new_with_label ("Perspective projection");
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), button1);
        gtk_check_menu_item_set_draw_as_radio(GTK_CHECK_MENU_ITEM(button1), 1);

        GtkWidget * button2 = gtk_check_menu_item_new_with_label ("Orthographic projection");
        gtk_check_menu_item_set_draw_as_radio(GTK_CHECK_MENU_ITEM(button2), 1);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), button2);

        gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (layer_cam_pos.perspectiveness == 1.0 ? button1 : button2), TRUE);

        g_signal_connect(button1, "activate",
                         (GCallback) menu_action_perspective, state);
        g_signal_connect(button2, "activate",
                         (GCallback) menu_action_ortho, state);

        // Interface modes
        GtkWidget * iface_wrapper = gtk_menu_item_new_with_label("Interface mode");
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), iface_wrapper);
        {
            GtkWidget * iface_menu = gtk_menu_new();
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(iface_wrapper), iface_menu);

            GtkWidget * iface_20d = gtk_check_menu_item_new_with_label("2.0D");
            gtk_menu_shell_append(GTK_MENU_SHELL(iface_menu), iface_20d);
            gtk_check_menu_item_set_draw_as_radio(GTK_CHECK_MENU_ITEM(iface_20d), 1);

            GtkWidget * iface_25d = gtk_check_menu_item_new_with_label("2.5D");
            gtk_menu_shell_append(GTK_MENU_SHELL(iface_menu), iface_25d);
            gtk_check_menu_item_set_draw_as_radio(GTK_CHECK_MENU_ITEM(iface_25d), 1);

            GtkWidget * iface_30d = gtk_check_menu_item_new_with_label("3.0D");
            gtk_menu_shell_append(GTK_MENU_SHELL(iface_menu), iface_30d);
            gtk_check_menu_item_set_draw_as_radio(GTK_CHECK_MENU_ITEM(iface_30d), 1);

            float iface = default_cam_mgr_get_interface_mode(linfo->cam_mgr);
            GtkWidget * active = NULL;
            if (iface ==  2.0f) {
                active = iface_20d;
            } else if (iface ==  2.5f) {
                active = iface_25d;
            } else if (iface == 3.0f) {
                active = iface_30d;
            } else {
                printf("WRN: iface mode %f not supported by menu!\n", iface);
                active = iface_25d;
            }
            gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (active), TRUE);


            g_signal_connect(iface_20d, "activate", (GCallback) menu_action_iface_20d, state);
            g_signal_connect(iface_25d, "activate", (GCallback) menu_action_iface_25d, state);
            g_signal_connect(iface_30d, "activate", (GCallback) menu_action_iface_30d, state);

        }


    }

    {
        GtkWidget * labelwrapper = gtk_menu_item_new();
        GtkWidget * label = gtk_label_new("Foo");
        gtk_label_set_markup(GTK_LABEL(label), "<b>Display Options</b>");
        gtk_container_add(GTK_CONTAINER(labelwrapper), label); // Auto centers? I guess that's fine
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), labelwrapper);

        GtkWidget * showBM = gtk_menu_item_new_with_label("Show Buffer/Layer Manager");
        g_signal_connect(showBM, "activate",(GCallback) menu_action_buffer_manager, state);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), showBM);

        GtkWidget * scene = gtk_menu_item_new_with_label("Save Vx Scene");
        g_signal_connect(scene, "activate",(GCallback) menu_action_scene, state);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), scene);

        GtkWidget * shot = gtk_menu_item_new_with_label("Save Screenshot");
        g_signal_connect(shot, "activate",(GCallback) menu_action_screen_shot, state);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), shot);

        if (state->movie_file == NULL) {
            GtkWidget * movie = gtk_menu_item_new_with_label("Record movie");
            g_signal_connect(movie, "activate",(GCallback) menu_action_record_movie, state);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), movie);
        } else {
            GtkWidget * movie = gtk_menu_item_new_with_label("Stop movie");
            g_signal_connect(movie, "activate",(GCallback) menu_action_stop_movie, state);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), movie);
        }

    }


    gtk_widget_show_all(menu);

    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
                   /* (event != NULL) ? event->button : 0, */
                   0,
                   gtk_get_current_event_time());
    //gdk_event_get_time((GdkEvent*)event));
}


static render_info_t * render_info_create()
{
    render_info_t * rinfo = calloc(1, sizeof(render_info_t));
    rinfo->layers = zarray_create(sizeof(layer_info_t*));
    rinfo->camera_positions = zhash_create(sizeof(uint32_t), sizeof(vx_camera_pos_t *), zhash_uint32_hash, zhash_uint32_equals);
    rinfo->layer_positions = zhash_create(sizeof(uint32_t), sizeof(uint32_t *), zhash_uint32_hash, zhash_uint32_equals);
    return rinfo;
}

static void movie_frame_destroy(movie_frame_t * mf)
{
    free(mf->buf);
    free(mf);
}

static void  render_info_destroy(render_info_t * rinfo)
{
    zarray_destroy(rinfo->layers); // just pointers

    zhash_vmap_values(rinfo->camera_positions, vx_camera_pos_destroy);
    zhash_destroy(rinfo->camera_positions);

    zhash_vmap_values(rinfo->layer_positions, free);
    zhash_destroy(rinfo->layer_positions);

    free(rinfo);
}


static void checkVersions()
{

    const char *glVersion = (const char *)glGetString(GL_VERSION);
    int major = -1, minor = -1;

    if (sscanf(glVersion, "%d.%d", &major, &minor) != 2 || major < MIN_GL_MAJOR_VERSION) {
        printf("ERR: Unsupported GL version %d.%d parsed from %s. Min. major version is %d\n",
               major, minor, glVersion, MIN_GL_MAJOR_VERSION);
        exit(1);
    }

    const GLubyte *glslVersion =
        glGetString( GL_SHADING_LANGUAGE_VERSION );
    if (verbose) printf("GLSL version %s\n",glslVersion);
}

static void gl_destroy(void * unused)
{
    task_thread_destroy(gl_thread);
    gl_thread = NULL;
    glcontext_X11_destroy(glc);
    glc = NULL;

}

static void gl_init(void * unused)
{
    if (verbose) printf("Creating GL context\n");
    glc = glcontext_X11_create();
    checkVersions(); // check version after we got a gl context

    vx_global_register_destroy(gl_destroy, NULL);
}

static void process_layer(state_t * state, vx_code_input_stream_t * cins)
{
    // update (or create) the layer_info_t

    int layer_id = cins->read_uint32(cins);
    int world_id = cins->read_uint32(cins);
    int draw_order = cins->read_uint32(cins);
    int bg_color = cins->read_uint32(cins);

    layer_info_t * linfo = NULL;
    zhash_get(state->layer_info_map, &layer_id, &linfo);


    if (linfo == NULL) {

        linfo = calloc(1,sizeof(layer_info_t));

        linfo->cam_mgr = default_cam_mgr_create();
        linfo->event_handler = default_event_handler_create(linfo->cam_mgr, UI_ANIMATE_MS);
        linfo->vp_mgr = vx_viewport_mgr_create();

        zhash_put(state->layer_info_map, &layer_id, &linfo, NULL, NULL);
    }

    linfo->layer_id = layer_id;
    linfo->draw_order = draw_order;

    bg_color+=world_id; // XXX unused variable
}

void process_camera_codes(state_t * state, vx_code_input_stream_t * cins)
{
    int layer_id = cins->read_uint32(cins);
    int op = cins->read_uint32(cins);

    layer_info_t * linfo = NULL;
    zhash_get(state->layer_info_map, &layer_id, &linfo);
    if (linfo == NULL)
        return;

    uint64_t mtime;
    vx_camera_pos_t * cur_pos;

    if (verbose) printf("Op %d remaining %d\n",op,  vx_code_input_stream_available(cins));

    double eyedf[3];
    double lookatdf[3];
    double updf[3];
    double xy0[2];
    double xy1[2];

    switch(op) {
        case OP_DEFAULTS:
            default_cam_mgr_defaults(linfo->cam_mgr, UI_ANIMATE_MS);
            break;

        case OP_PROJ_PERSPECTIVE:
        case OP_PROJ_ORTHO:
            cur_pos = default_cam_mgr_get_cam_target(linfo->cam_mgr,&mtime);
            cur_pos->perspectiveness = op == OP_PROJ_ORTHO ? 0.0f : 1.0f;
            default_cam_mgr_set_cam_pos(linfo->cam_mgr, cur_pos, 0, 0);
            vx_camera_pos_destroy(cur_pos);
            break;

        case OP_LOOKAT:
            eyedf[0] = cins->read_float(cins);
            eyedf[1] = cins->read_float(cins);
            eyedf[2] = cins->read_float(cins);

            lookatdf[0] = cins->read_float(cins);
            lookatdf[1] = cins->read_float(cins);
            lookatdf[2] = cins->read_float(cins);

            updf[0] = cins->read_float(cins);
            updf[1] = cins->read_float(cins);
            updf[2] = cins->read_float(cins);
            mtime = cins->read_uint32(cins); // animate ms ignored! XXX

            default_cam_mgr_lookat(linfo->cam_mgr, eyedf, lookatdf, updf, cins->read_uint8(cins), mtime);
            break;

        case OP_FIT2D:
            xy0[0] = cins->read_float(cins);
            xy0[1] = cins->read_float(cins);

            xy1[0] = cins->read_float(cins);
            xy1[1] = cins->read_float(cins);
            mtime = cins->read_uint32(cins); // animate ms ignored! XXX

            default_cam_mgr_fit2D(linfo->cam_mgr, xy0, xy1, cins->read_uint8(cins), mtime);
            break;
        case OP_FOLLOW_MODE_DISABLE:
            default_cam_mgr_follow_disable(linfo->cam_mgr);

            break;

        case OP_INTERFACE_MODE:
        {
            float mode = cins->read_float(cins);
            default_cam_mgr_set_interface_mode(linfo->cam_mgr, mode);
        }
    }

    if (op == OP_FOLLOW_MODE) {
        double pos3[3];
        double quat4[4];

        pos3[0] = cins->read_double(cins);
        pos3[1] = cins->read_double(cins);
        pos3[2] = cins->read_double(cins);

        quat4[0] = cins->read_double(cins);
        quat4[1] = cins->read_double(cins);
        quat4[2] = cins->read_double(cins);
        quat4[3] = cins->read_double(cins);

        int followYaw = cins->read_uint32(cins);
        uint32_t animate_ms = cins->read_uint32(cins);

        default_cam_mgr_follow(linfo->cam_mgr, pos3, quat4, followYaw, animate_ms);
    }
}

static void movie(state_t * state, vx_code_input_stream_t * cins, int record)
{
    if (record) {
        if (state->movie_file != NULL) {
            printf("WRN: Only one movie can be recorded at a time\n");
            return;
        }

        uint64_t mtime = vx_util_mtime();
        time_t now  = time(NULL);
        struct tm * now2 = localtime(&now);

        pthread_mutex_lock(&state->movie_mutex);
        if (vx_code_input_stream_available(cins) > 0) {
            const char * f2 = cins->read_str(cins);
            state->movie_file = strdup(f2);
        } else { // generate a unique ID
            state->movie_file = sprintf_alloc("m%4d%02d%02d_%02d%02d%02d_%03d.ppms.gz",
                                     now2->tm_year + 1900,
                                     now2->tm_mon + 1,
                                     now2->tm_mday,
                                     now2->tm_hour,
                                     now2->tm_min,
                                     now2->tm_sec,
                                     (int)(mtime%1000));
        }
        pthread_cond_signal(&state->movie_cond);

        pthread_mutex_unlock(&state->movie_mutex);

    } else { // finish recording
        if (state->movie_file == NULL) {
            printf("WRN: Can't stop recording a movie: none in progress\n");
            return;
        }


        pthread_mutex_lock(&state->movie_mutex);
        {
            state->movie_file = NULL;
            pthread_cond_signal(&state->movie_cond);
        }
        pthread_mutex_unlock(&state->movie_mutex);

    }
}

static void save_scene(state_t * state, vx_code_input_stream_t * cins)
{
    char * filename = NULL;
    if (vx_code_input_stream_available(cins) > 0) {
        const char * f2 = cins->read_str(cins);
        filename = strdup(f2);
    } else { // generate a unique ID

        uint64_t mtime = vx_util_mtime();
        time_t now  = time(NULL);
        struct tm * now2 = localtime(&now);

        filename=sprintf_alloc("v%4d%02d%02d_%02d%02d%02d_%03d.vxs",
                               now2->tm_year + 1900,
                               now2->tm_mon + 1,
                               now2->tm_mday,
                               now2->tm_hour,
                               now2->tm_min,
                               now2->tm_sec,
                               (int)(mtime % 1000));
    }

    vx_code_output_stream_t * couts = vx_gl_renderer_serialize(state->glrend);

    // Append camera positions
    if (state->last_render_info != NULL) {
        render_info_t *rinfo = state->last_render_info;

        for (int i = 0; i <4; i++)
            couts->write_uint32(couts, rinfo->viewport[i]);

        couts->write_uint32(couts, zarray_size(rinfo->layers));
        for (int i = 0; i < zarray_size(rinfo->layers); i++) {
            layer_info_t * linfo = NULL;
            zarray_get(rinfo->layers, i, &linfo);

            couts->write_uint32(couts, linfo->layer_id);

            vx_camera_pos_t * pos = NULL;
            int * viewport = NULL;
            zhash_get(rinfo->camera_positions, &linfo->layer_id, &pos);
            zhash_get(rinfo->layer_positions, &linfo->layer_id, &viewport);

            vx_tcp_util_pack_camera_pos(pos, couts);


            for (int i = 0; i < 4; i++)
                couts->write_uint32(couts, viewport[i]);
        }
    }



    FILE * fp = fopen(filename, "w");
    fwrite(couts->data, sizeof(uint8_t), couts->pos, fp);
    fclose(fp);
    printf("Wrote %d bytes to %s\n", couts->pos, filename);

    vx_code_output_stream_destroy(couts);
    free(filename);

}

static void screen_shot(state_t * state, vx_code_input_stream_t * cins)
{
    char * filename = NULL;
    if (vx_code_input_stream_available(cins) > 0) {
        const char * f2 = cins->read_str(cins);
        filename = strdup(f2);
    } else { // generate a unique ID

        uint64_t mtime = vx_util_mtime();
        time_t now  = time(NULL);
        struct tm * now2 = localtime(&now);

        filename=sprintf_alloc("p%4d%02d%02d_%02d%02d%02d_%03d.png",
                               now2->tm_year + 1900,
                               now2->tm_mon + 1,
                               now2->tm_mday,
                               now2->tm_hour,
                               now2->tm_min,
                               now2->tm_sec,
                               (int)(mtime % 1000));
    }


    int last_idx = (state->cur_pb_idx + 1) % 2;

    GdkPixbuf * pb = state->pixbufs[last_idx];

    if (pb == NULL)
        return;

    GError * err = NULL;
    gdk_pixbuf_save(pb,filename, "png", & err, NULL);

    printf("Saved screenshot to %s\n", filename);
    free(filename);

}

static void process_viewport_rel(state_t * state, vx_code_input_stream_t * cins)
{
    int layer_id = cins->read_uint32(cins);

    float rel[] = {cins->read_float(cins),
                   cins->read_float(cins),
                   cins->read_float(cins),
                   cins->read_float(cins)};
    int animate_ms = cins->read_uint32(cins);
    uint64_t mtime_goal = vx_util_mtime() + animate_ms;

    layer_info_t * linfo = NULL;
    zhash_get(state->layer_info_map, &layer_id, &linfo);
    if (linfo == NULL)
        return;

    vx_viewport_mgr_set_rel(linfo->vp_mgr,rel, mtime_goal);
}

static void process_viewport_abs(state_t * state, vx_code_input_stream_t * cins)
{

    int layer_id = cins->read_uint32(cins);
    int align_code = cins->read_uint32(cins);

    int offx = cins->read_uint32(cins);
    int offy = cins->read_uint32(cins);

    int width = cins->read_uint32(cins);
    int height = cins->read_uint32(cins);

    int animate_ms = cins->read_uint32(cins);
    uint64_t mtime_goal = vx_util_mtime() + animate_ms;

    layer_info_t * linfo = NULL;
    zhash_get(state->layer_info_map, &layer_id, &linfo);
    if (linfo == NULL)
        return;

    vx_viewport_mgr_set_abs(linfo->vp_mgr, align_code,
                            offx, offy, width, height, mtime_goal);

}

static void send_codes(vx_display_t * disp, const uint8_t * data, int datalen)
{
    state_t * state = disp->impl;

    vx_code_input_stream_t * cins = vx_code_input_stream_create(data, datalen);
    // Peek at the code type to determine which function to call
    uint32_t code = cins->read_uint32(cins);
    pthread_mutex_lock(&state->mutex);
    switch(code) {
        case OP_BUFFER_RESOURCES:
            if (state->mgr)
                vx_resc_manager_buffer_resources(state->mgr, data, datalen);
            break;

        case OP_BUFFER_ENABLED:
            vx_gl_renderer_buffer_enabled(state->glrend, cins);
            break;
        case OP_BUFFER_CODES:
            vx_gl_renderer_set_buffer_render_codes(state->glrend, cins);
            break;
        case OP_LAYER_INFO:
            vx_gl_renderer_update_layer(state->glrend, cins);
            {
                vx_code_input_stream_t * c2 = vx_code_input_stream_create(data, datalen);
                c2->read_uint32(c2); //discard code

                process_layer(state, c2);
                vx_code_input_stream_destroy(c2);
            }
            // XXX Also need to intercept these!
            break;
        case OP_DEALLOC_RESOURCES:
            vx_gl_renderer_remove_resources(state->glrend, cins);
            break;

        case OP_LAYER_VIEWPORT_REL:
            process_viewport_rel(state, cins);
            break;
        case OP_LAYER_VIEWPORT_ABS:
            process_viewport_abs(state, cins);
            break;

        case OP_LAYER_CAMERA:
            process_camera_codes(state, cins);
            break;

        case OP_WINDOW_SCENE: {
            save_scene(state, cins);
            break;
        }

        case OP_WINDOW_SCREENSHOT:
            screen_shot(state, cins);
            break;
        case OP_WINDOW_MOVIE_RECORD:
        case OP_WINDOW_MOVIE_STOP:
            movie(state, cins, code == OP_WINDOW_MOVIE_RECORD);
            break;
        default:
            assert(0);
    }
    pthread_mutex_unlock(&state->mutex);

    // Buffer layer manager, outside the lock
    switch(code) {
        case OP_BUFFER_ENABLED:
            vx_gtk_buffer_manager_codes(state->buffer_manager, data, datalen);
            break;
        case OP_BUFFER_CODES:
            vx_gtk_buffer_manager_codes(state->buffer_manager, data, datalen);
            break;
        case OP_LAYER_INFO:
            vx_gtk_buffer_manager_codes(state->buffer_manager, data, datalen);
            break;
    }


    vx_code_input_stream_destroy(cins);
}

static void send_resources(vx_display_t * disp, zhash_t * all_resources)
{
    state_t * state = disp->impl;
    pthread_mutex_lock(&state->mutex);

    if (state->mgr) {
        // manage these resources!
        zhash_t * transmit = vx_resc_manager_dedup_resources(state->mgr, all_resources);
        vx_gl_renderer_add_resources(state->glrend, transmit);
        zhash_destroy(transmit);
    } else { // when resource management is already done, just pass through
        vx_gl_renderer_add_resources(state->glrend, all_resources);
    }
    pthread_mutex_unlock(&state->mutex);
}

static void add_listener(vx_display_t * disp, vx_display_listener_t * listener)
{
    state_t * state = disp->impl;
    pthread_mutex_lock(&state->listener_mutex);
    zarray_add(state->listeners, &listener);
    pthread_mutex_unlock(&state->listener_mutex);
}

static void remove_listener(vx_display_t * disp, vx_display_listener_t * listener)
{
    state_t * state = disp->impl;
    pthread_mutex_lock(&state->listener_mutex);
    zarray_remove_value(state->listeners, &listener, 0);
    pthread_mutex_unlock(&state->listener_mutex);
}

// must be run on gl thread
static void gl_cleanup_task(void * data)
{
    state_t * state = data;

    gl_fbo_destroy(state->fbo);
    vx_gl_renderer_destroy(state->glrend); // XXX This actually needs to run on the GL thread

}

// must be run on gl thread
static void render_task(void * data)
{
    render_buffer_t * buffer = data;
    state_t * state = buffer->state;

    // Check whether we have a FBO of the correct size
    if (state->fbo == NULL || state->fbo_width != buffer->width || state->fbo_height != buffer->height) {
        if(state->fbo != NULL)
            gl_fbo_destroy(state->fbo);

        assert(glc != NULL);
        state->fbo = gl_fbo_create(glc, buffer->width, buffer->height);
        state->fbo_width = buffer->width;
        state->fbo_height = buffer->height;
        if (verbose) printf("Allocated FBO of dimension %d %d\n",state->fbo_width, state->fbo_height);
        buffer->rendered = 1; //force a render if the fbo changes
    }
    if (state->fbo == NULL) {
        printf("Abort render because bad fbo\n");
        buffer->rendered = 0;
        return;
    }
    gl_fbo_bind(state->fbo);

    if (!buffer->rendered && !vx_gl_renderer_changed_since_last_render(buffer->state->glrend))
        return;

    // do all the gl drawing calls
    vx_gl_renderer_draw_frame(state->glrend, buffer->width, buffer->height);

    // read the frame buffer
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, buffer->width, buffer->height, buffer->format, GL_UNSIGNED_BYTE, buffer->out_buf);

    buffer->rendered = 1;
}

static int zvx_layer_info_compare (const void *a_ptr_ptr, const void *b_ptr_ptr)
{
    layer_info_t *li1 = *(void **)a_ptr_ptr;
    layer_info_t *li2 = *(void **)b_ptr_ptr;
    return li1->draw_order - li2->draw_order;
}

// this loop tries to run at X fps, and issue render commands
static void * render_loop(void * foo)
{
    state_t * state = foo;
    if (verbose)printf("Starting render thread!\n");

    uint64_t render_count = 0;
    uint64_t last_mtime = vx_util_mtime();
    double avgDT = 1.0f/state->target_frame_rate;
    uint64_t avg_loop_us = 3000; // initial render time guess
    while (state->rendering) {
        int64_t sleeptime = (1000000 / state->target_frame_rate) - (int64_t) avg_loop_us;
        if (sleeptime > 0)
            usleep(sleeptime); // XXX fix to include render time

        // Diagnostic tracking
        uint64_t mtime_start = vx_util_mtime(); // XXX
        avgDT = avgDT*.9 + .1 * (mtime_start - last_mtime)/1000;
        last_mtime = mtime_start;
        render_count++;

        if (verbose) {
            if (render_count % 100 == 0)
                printf("Average render DT = %.3f FPS = %.3f avgloopus %"PRIu64" sleeptime = %"PRIi64"\n",
                       avgDT, 1.0/avgDT, avg_loop_us, sleeptime);
        }

        // prep the render data
        render_buffer_t rbuf;
        rbuf.state = state;
        rbuf.width = gtku_image_pane_get_width(state->imagePane);
        rbuf.height = gtku_image_pane_get_height(state->imagePane);
        if (rbuf.width == 0 && rbuf.height == 0)
            continue; // if the viewport is 0,0

        // smartly reuse, or reallocate the output pixel buffer when resizing occurs
        GdkPixbuf * pixbuf = state->pixbufs[state->cur_pb_idx];
        if (pixbuf == NULL || gdk_pixbuf_get_width(pixbuf) != rbuf.width || gdk_pixbuf_get_height(pixbuf) != rbuf.height) {
            if (pixbuf != NULL) {
                g_object_unref(pixbuf);
                free(state->pixdatas[state->cur_pb_idx]);
            }

            state->pixdatas[state->cur_pb_idx] = malloc(rbuf.width*rbuf.height*3); // can't stack allocate, can be too big (retina)
            pixbuf = gdk_pixbuf_new_from_data(state->pixdatas[state->cur_pb_idx], GDK_COLORSPACE_RGB, FALSE, 8,
                                              rbuf.width, rbuf.height, rbuf.width*3, NULL, NULL); // no destructor fn for pix data, handle manually
            state->pixbufs[state->cur_pb_idx] = pixbuf;
        }

        // second half of init:
        rbuf.out_buf = gdk_pixbuf_get_pixels(pixbuf);
        rbuf.format = GL_RGB;
        rbuf.rendered = 0;

        // 1 compute all the viewports
        render_info_t * rinfo = render_info_create();
        rinfo->viewport[0] = rinfo->viewport[1] = 0;
        rinfo->viewport[2] = rbuf.width;
        rinfo->viewport[3] = rbuf.height;

        {
            zhash_iterator_t itr;
            uint32_t layer_id = 0;
            layer_info_t * linfo = NULL;
            zhash_iterator_init(state->layer_info_map, &itr);
            while(zhash_iterator_next(&itr, &layer_id, &linfo)){
                zarray_add(rinfo->layers, &linfo);
            }
            zarray_sort(rinfo->layers, zvx_layer_info_compare);
        }

        zarray_t * fp = zarray_create(sizeof(matd_t*));

        matd_t *mm = matd_create(4,4); zarray_add(fp, &mm);
        matd_t *pm = matd_create(4,4); zarray_add(fp, &pm);

        pthread_mutex_lock(&state->mutex);
        for (int i = 0; i < zarray_size(rinfo->layers); i++) {
            layer_info_t *linfo = NULL;
            zarray_get(rinfo->layers, i, &linfo);

            int * viewport = vx_viewport_mgr_get_pos(linfo->vp_mgr, rinfo->viewport, mtime_start);
            vx_camera_pos_t *pos = default_cam_mgr_get_cam_pos(linfo->cam_mgr, viewport, mtime_start);

            // store viewport, pos
            zhash_put(rinfo->layer_positions, &linfo->layer_id, &viewport, NULL, NULL);
            zhash_put(rinfo->camera_positions, &linfo->layer_id, &pos, NULL, NULL);

            // feed the actual camera/projection matrix to the gl side
            vx_camera_pos_model_matrix(pos,mm->data);
            vx_camera_pos_projection_matrix(pos, pm->data);

            matd_t * pmmm = matd_multiply(pm,mm); zarray_add(fp, &pmmm);

            float pm16[16];
            vx_util_copy_floats(pmmm->data, pm16, 16);

            float eye3[16];
            vx_util_copy_floats(pos->eye, eye3, 3);

            vx_gl_renderer_set_layer_render_details(state->glrend, linfo->layer_id, viewport, pm16, eye3);
        }

        // 2 Render the data
        task_thread_schedule_blocking(gl_thread, render_task, &rbuf);

        render_info_t * old = state->last_render_info;
        state->last_render_info = rinfo;

        pthread_mutex_unlock(&state->mutex);


        // 3 if a render occurred, then swap gtk buffers
        if (rbuf.rendered) {

            // point to the correct buffer for the next render:
            state->cur_pb_idx = (state->cur_pb_idx +1 ) % 2;

            // flip y coordinate in place:
            vx_util_flipy(rbuf.width*3, rbuf.height, rbuf.out_buf);

            // swap the image's backing buffer
            g_object_ref(pixbuf); // XXX Since gtku always unrefs with each of these calls, increment accordingly
            gtku_image_pane_set_buffer(state->imagePane, pixbuf);
        }

        // 3.1 If a movie is in progress, also need to serialize the frame
        pthread_mutex_lock(&state->movie_mutex);
        if (state->movie_file != NULL) {

            int last_idx = (state->cur_pb_idx + 1) % 2;
            GdkPixbuf * pb = state->pixbufs[last_idx];

            movie_frame_t * movie_img = calloc(1, sizeof(movie_frame_t));
            movie_img->mtime = mtime_start;
            movie_img->width = gdk_pixbuf_get_width(pb);
            movie_img->height = gdk_pixbuf_get_height(pb);
            movie_img->stride = 3*movie_img->width;
            movie_img->buf = malloc(movie_img->stride*movie_img->height);
            memcpy(movie_img->buf, state->pixdatas[last_idx], movie_img->stride*movie_img->height);


            // Alloc in this thread, dealloc in movie thread
            zarray_add(state->movie_pending, & movie_img);

            pthread_cond_signal(&state->movie_cond);

        }
        pthread_mutex_unlock(&state->movie_mutex);

        // Compare the camera postions in rinfo to old,
        // and issue camera changed events for each layer where the
        // structs are not identical
        {
            zarray_t * layer_ids = zhash_keys(rinfo->camera_positions);

            for (int i = 0; i < zarray_size(layer_ids); i++) {
                int layer_id = 0;
                zarray_get(layer_ids, i, &layer_id);

                vx_camera_pos_t * cur_pos = NULL;
                vx_camera_pos_t * old_pos = NULL;

                zhash_get(rinfo->camera_positions, &layer_id, &cur_pos);
                if (old != NULL)
                    zhash_get(old->camera_positions, &layer_id, &old_pos);

                // XXX Do we also want to notify each

                assert(cur_pos != NULL);

                if (old == NULL || old_pos == NULL || memcmp(cur_pos, old_pos, sizeof(vx_camera_pos_t)) != 0) {
                    // Flag a change
                    dispatch_event_t * disp_event = calloc(1,sizeof(dispatch_event_t));
                    disp_event->type = DISPATCH_TYPE_CAMERA;
                    disp_event->layer_id = layer_id;
                    disp_event->pos = calloc(1, sizeof(vx_camera_pos_t));
                    memcpy(disp_event->pos, cur_pos, sizeof(vx_camera_pos_t));

                    disp_event->data = NULL;

                    dispatch_event(state, disp_event);
                }
            }

            zarray_destroy(layer_ids);
        }

        // cleanup
        if (old)
            render_info_destroy(old);
        zarray_vmap(fp, matd_destroy);
        zarray_destroy(fp);


        uint64_t mtime_end = vx_util_mtime();
        avg_loop_us = (uint64_t)(.5*avg_loop_us + .5 * 1000 * (mtime_end - mtime_start));
    }
    if (verbose) printf("Render thread exiting\n");

    pthread_exit(NULL);

}

int vx_gtk_display_dispatch_key(vx_display_t * disp, vx_key_event_t *event)
{
    assert(disp->impl_type == VX_GTK_DISPLAY_IMPL);
    state_t * state = disp->impl;

    // Process key shortcuts for the display
    pthread_mutex_lock(&state->mutex);

    int mouse_pressed_layer_id = state->mouse_pressed_layer_id; //local copy

    // Also dispatch to the relevant layers camera manager
    {
        layer_info_t * linfo = NULL;
        zhash_get(state->layer_info_map, &mouse_pressed_layer_id, &linfo);
        if (linfo != NULL) // sometimes there could be no layers
            linfo->event_handler->key_event(linfo->event_handler, NULL, event);
    }

    pthread_mutex_unlock(&state->mutex);

    {
        dispatch_event_t * disp_event = calloc(1,sizeof(dispatch_event_t));
        disp_event->type = DISPATCH_TYPE_KEY_EVENT;
        disp_event->layer_id = mouse_pressed_layer_id;
        disp_event->pos = NULL;
        disp_event->data = calloc(1, sizeof(vx_key_event_t));
        memcpy(disp_event->data, event, sizeof(vx_key_event_t));

        dispatch_event(state, disp_event);
    }

    return 1; // gobble all events
}

int vx_gtk_display_dispatch_mouse(vx_display_t * disp, vx_mouse_event_t *event)
{
    assert(disp->impl_type == VX_GTK_DISPLAY_IMPL);
    state_t * state = disp->impl;

    pthread_mutex_lock(&state->mutex);
    render_info_t * rinfo = state->last_render_info;
    if (rinfo == NULL) {
        pthread_mutex_unlock(&state->mutex);
        return 1;
    }

    {// Make a copy of the event, whose memory we will manage
        vx_mouse_event_t * tmp = event;
        event  = malloc(sizeof(vx_mouse_event_t));
        memcpy(event, tmp, sizeof(vx_mouse_event_t));
    }

    // Determine which layer is focused:
    vx_mouse_event_t * last_event = state->last_mouse_event;
    state->last_mouse_event = event;
    if (!last_event ) { //XXX what to do here? (first time)
        pthread_mutex_unlock(&state->mutex);
        return 1;
    }

    uint32_t bdiff = event->button_mask ^ last_event->button_mask;
    free(last_event);

    // 1 Each time the mouse is pushed down, the layer_id is set to the layer under the mouse
    // 2 if the mouse is not pushed down, the layer under the mouse is selected

    int layer_under_mouse_id = pick_layer(rinfo, event->x, event->y);
    if (layer_under_mouse_id == 0) {
        pthread_mutex_unlock(&state->mutex);
        return 1; // there may be no layers
    }

    int button_clicked = bdiff && (bdiff & event->button_mask);
    int button_down = event->button_mask;

    if (!button_clicked && button_down) {
        // leave the layer id as is
    } else if (layer_under_mouse_id != 0){
        state->mouse_pressed_layer_id = layer_under_mouse_id;
    } else {
        // no mouse under layer -- don't change the id?
    }
    int mouse_pressed_layer_id = state->mouse_pressed_layer_id; //local copy

    vx_camera_pos_t * mouse_pressed_layer_pos_orig = NULL;
    zhash_get(rinfo->camera_positions, &mouse_pressed_layer_id, &mouse_pressed_layer_pos_orig);
    vx_camera_pos_t * mouse_pressed_layer_pos = calloc(1,sizeof(vx_camera_pos_t)); // local copy
    memcpy(mouse_pressed_layer_pos,mouse_pressed_layer_pos_orig, sizeof(vx_camera_pos_t));

    // Also dispatch to the relevant layers camera manager
    {
        layer_info_t * linfo = NULL;
        zhash_get(state->layer_info_map, &mouse_pressed_layer_id, &linfo);
        if (linfo != NULL) // sometimes there could be no layers
            linfo->event_handler->mouse_event(linfo->event_handler, NULL, mouse_pressed_layer_pos, event);
    }
    pthread_mutex_unlock(&state->mutex);

    {
        dispatch_event_t * disp_event = calloc(1,sizeof(dispatch_event_t));
        disp_event->type = DISPATCH_TYPE_MOUSE_EVENT;
        disp_event->layer_id = mouse_pressed_layer_id;
        disp_event->pos = calloc(1, sizeof(vx_camera_pos_t));
        memcpy(disp_event->pos, mouse_pressed_layer_pos, sizeof(vx_camera_pos_t));

        disp_event->data = calloc(1, sizeof(vx_mouse_event_t));
        memcpy(disp_event->data, event, sizeof(vx_mouse_event_t));

        dispatch_event(state, disp_event);
    }

    free(mouse_pressed_layer_pos);
    return 1; //gobble all events
}

static void* dispatch_run(void * data)
{
    state_t * state = data;

    zarray_t * events  = zarray_create(sizeof(dispatch_event_t*));

    while (state->rendering) {
        pthread_mutex_lock(&state->dispatch_mutex);
        while (state->rendering && zarray_size(state->dispatch_queue) == 0)
            pthread_cond_wait(&state->dispatch_cond, &state->dispatch_mutex);

        if (!state->rendering) { // exit signaled while waiting on cond
            pthread_mutex_unlock(&state->dispatch_mutex);
            break;
        }
        zarray_add_all(events, state->dispatch_queue);
        zarray_clear(state->dispatch_queue);
        pthread_mutex_unlock(&state->dispatch_mutex);

        static int wrn_once = 0;
        if (!wrn_once && zarray_size(events) > 128) {
            printf("ERR (onetime warning): Event dispatch is falling behind! Waiting to dispatch %d events\n", zarray_size(events));
            wrn_once = 1;
        }


        // process all pending events, notifying all listeners
        while (zarray_size(events) > 0) {
            dispatch_event_t * event = NULL;
            zarray_get(events, 0, &event);
            zarray_remove_index(events, 0, 0);


            // notify all listeners of this event
            pthread_mutex_lock(&state->listener_mutex);
            for (int i = 0; i < zarray_size(state->listeners); i++) {
                vx_display_listener_t * l = NULL;
                zarray_get(state->listeners, i, &l);
                switch(event->type) {
                    case DISPATCH_TYPE_MOUSE_EVENT:
                        l->event_dispatch_mouse(l, event->layer_id, event->pos, event->data);
                        break;
                    case DISPATCH_TYPE_KEY_EVENT:
                        l->event_dispatch_key(l, event->layer_id, event->data);
                        break;
                    case DISPATCH_TYPE_CAMERA:
                        l->camera_changed(l, event->layer_id, event->pos);
                        break;
                }
            }
            pthread_mutex_unlock(&state->listener_mutex);


            dispatch_event_destroy(event);
        }
    }

    return NULL;
}


static void * movie_run(void * params)
{
    state_t * state = params;

    zarray_t * frames = zarray_create(sizeof(movie_frame_t*));

    char * last_filename = NULL;
    char * current_filename = NULL;

    gzFile gzMovie = NULL;
    int movie_frames = 0;
    int lost_frames = 0;
    uint64_t movie_start_mtime = 0;

    while (state->rendering) {


        pthread_mutex_lock(&state->movie_mutex);
        while (state->rendering && current_filename == state->movie_file && zarray_size(state->movie_pending) == 0)
            pthread_cond_wait(&state->movie_cond, &state->movie_mutex);

        if (!state->rendering) { // exit signaled while waiting on cond
            pthread_mutex_unlock(&state->movie_mutex);
            break;
        }

        for (int i = 0; i < zarray_size(state->movie_pending); i++) {
            movie_frame_t * tmp = NULL;
            zarray_get(state->movie_pending, i, &tmp);
            zarray_add(frames, &tmp);
        }
        zarray_clear(state->movie_pending);
        current_filename = state->movie_file;

        pthread_mutex_unlock(&state->movie_mutex);


        // Depending on setup, execute different actions
        // 1) Open 2) Close 3) Write a frame

        if (zarray_size(frames) > 0 && current_filename != NULL) {

            // Write the most recent frame, dump the rest
            movie_frame_t * movie_img = NULL;
            zarray_get(frames, zarray_size(frames)-1, &movie_img);
            zarray_remove_index(frames, zarray_size(frames)-1, 0);

            char header[256];
            sprintf(header, "#mtime=%"PRIu64"\nP6 %d %d %d\n", movie_img->mtime, movie_img->width, movie_img->height, 255);
            int header_len = strlen(header);
            int imglen = movie_img->stride*movie_img->height;// RGB format

            gzwrite(gzMovie, header, header_len);
            gzwrite(gzMovie, movie_img->buf, imglen);
            int res = gzflush(gzMovie, Z_SYNC_FLUSH);

            if (res != Z_OK) {
                int errval = 0;
                printf("Error writing movie %s. Closing file\n", gzerror(gzMovie, &errval));
                gzclose(gzMovie);
                free(current_filename);
                current_filename = NULL;
            }
            movie_frames++;
            movie_frame_destroy (movie_img);
        }
        // cleanup the frames:
        lost_frames += zarray_size(frames);
        zarray_vmap(frames, movie_frame_destroy);
        zarray_clear(frames);


        // Got a new filename, need to start recording
        if (current_filename != NULL && last_filename == NULL)
        {

            gzMovie = gzopen(current_filename, "w3");
            movie_frames = 0;
            lost_frames = 0;
            movie_start_mtime = vx_util_mtime();

            printf("NFO: Starting movie at %s\n", current_filename);

            if (gzMovie == NULL) {
                printf("WRN: Unable to start movie at %s\n", current_filename);
                free(current_filename); // XXX Would need to edit
                                        // state->movie_file ?
                current_filename = NULL;
            }
        }


        if (current_filename == NULL && last_filename != NULL)
        {

            gzclose(gzMovie);

            printf("NFO: Wrote/lost %d/%d movie frames at %.2f fps to %s\n",
                   movie_frames, lost_frames, 1e3 * movie_frames / (vx_util_mtime() - movie_start_mtime), last_filename);

            free(last_filename);
        }


        last_filename = current_filename;
    }

    // XXX Cleanup? might shutdown while recording?

    return NULL;
}


static state_t * state_create(vx_display_t * super)
{
    state_t * state = calloc(1, sizeof(state_t));
    state->super = super;
    state->glrend = vx_gl_renderer_create();


    // because the resource manager is called in send_codes(), and will itself call send_codes() again
    pthread_mutexattr_t mutexAttr;
    pthread_mutexattr_init(&mutexAttr);
    pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_RECURSIVE);

    pthread_mutex_init(&state->mutex, &mutexAttr);
    state->listeners = zarray_create(sizeof(vx_display_listener_t*));
    pthread_mutex_init(&state->listener_mutex, NULL);
    state->rendering = 1;
    state->target_frame_rate = 30.0f; // XXX
    state->buffer_manager = vx_gtk_buffer_manager_create(state->super);

    // dispatch
    state->dispatch_queue = zarray_create(sizeof(dispatch_event_t *));
    pthread_mutex_init(&state->dispatch_mutex, NULL);
    pthread_cond_init(&state->dispatch_cond, NULL);
    pthread_create(&state->dispatch_thread, NULL, dispatch_run, state);

    state->movie_pending = zarray_create(sizeof(movie_frame_t*));
    pthread_mutex_init(&state->movie_mutex, NULL);
    pthread_cond_init(&state->movie_cond, NULL);
    pthread_create(&state->movie_thread, NULL, movie_run, state);

    state->layer_info_map = zhash_create(sizeof(uint32_t), sizeof(layer_info_t*), zhash_uint32_hash, zhash_uint32_equals);

    return state;
}

static void layer_info_destroy(layer_info_t * linfo)
{
    default_cam_mgr_destroy(linfo->cam_mgr);
    vx_viewport_mgr_destroy(linfo->vp_mgr);
    //linfo->event_handler->destroy(linfo->event_handler); //XXX

    free(linfo);
}

static void state_destroy(state_t * state)
{
    if (verbose) printf("State destroying\n");
    state->rendering = 0;
    pthread_join(state->render_thread, NULL);
    if (verbose) printf("render thread joined\n");

    pthread_cond_signal(&state->movie_cond);
    pthread_join(state->movie_thread, NULL);
    if (verbose) printf("movie thread joined\n");

    zhash_vmap_values(state->layer_info_map, layer_info_destroy);
    zhash_destroy(state->layer_info_map); // XXX values

    for (int i =0; i < 2; i++) {
        free(state->pixdatas[i]);
        if (state->pixbufs[i] != NULL) {
            g_object_unref(state->pixbufs[i]);
        }
    }


    task_thread_schedule_blocking(gl_thread, gl_cleanup_task, state);

    pthread_mutex_destroy(&state->mutex);
    pthread_mutex_destroy(&state->movie_mutex);
    pthread_cond_destroy(&state->movie_cond);

    zarray_destroy(state->listeners);
    if (state->last_render_info != NULL)
        render_info_destroy(state->last_render_info);
    vx_resc_manager_destroy(state->mgr);
    vx_gtk_buffer_manager_destroy(state->buffer_manager);
    free(state);
}

vx_display_t * vx_gtk_display_create(GtkuImagePane *pane, int use_resc_mgr)
{
    vx_display_t * disp = calloc(1, sizeof(vx_display_t));
    state_t * state = state_create(disp);
    disp->impl = state;
    disp->impl_type = VX_GTK_DISPLAY_IMPL;
    if (use_resc_mgr)
        state->mgr = vx_resc_manager_create(disp);
    state->imagePane = pane;
    assert(pane != NULL);

    disp->send_codes = send_codes;
    disp->send_resources = send_resources;
    disp->add_listener = add_listener;
    disp->remove_listener = remove_listener;

    // GL thread. Ensure that only one gtk_display creates the gl thread
    if (gl_thread == NULL) {
        pthread_mutex_lock(&gl_init_mutex);
        if (gl_thread == NULL) {
            gl_thread = task_thread_create();

            task_thread_schedule_blocking(gl_thread, gl_init, NULL);
        }
        pthread_mutex_unlock(&gl_init_mutex);
    }

    pthread_create(&state->render_thread, NULL, render_loop, state);
    return disp;
}

void vx_gtk_display_destroy(vx_display_t * disp)
{
    state_destroy((state_t*)disp->impl);
    free(disp);
}
