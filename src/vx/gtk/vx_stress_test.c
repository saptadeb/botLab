#include <gtk/gtk.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <pthread.h>

// app wrappers
#include "vx/vx_remote_display_source.h"
#include "vx/gtk/vx_gtk_display_source.h"

// core api
#include "vx/vx_global.h"
#include "vx/vx_layer.h"
#include "vx/vx_world.h"
#include "vx/vx_util.h"

// drawables
#include "vx/vxo_drawables.h"
#include "vx/vx_colors.h"
#include "vx/vx_codes.h"

#include "common/getopt.h"
#include "common/image_util.h"
#include "common/matd.h"
#include "common/matd_coords.h"
#include "common/string_util.h"

#define CAM_RADIUS 20.0f
#define NRENDER 10

typedef struct
{
    int running;

    image_u8_t *img;

    vx_world_t * world;
    vx_world_t * world2;
    vx_world_t * world3;
    zhash_t * layers;

    vx_event_handler_t veh;
    vx_camera_listener_t cl;

    pthread_mutex_t mutex; // for accessing the arrays
    pthread_t render_threads[NRENDER];

    pthread_t camera_thread;
} state_t;



static void draw(state_t * state, vx_world_t * world)
{
    // Draw from the vx shape library
    vx_buffer_add_back(vx_world_get_buffer(world, "fixed-cube"), vxo_chain(vxo_mat_translate3(3.0,0,0),
                                                                           vxo_mat_scale(2),
                                                                           vxo_box(vxo_mesh_style(vx_orange))));
    vx_buffer_swap(vx_world_get_buffer(world, "fixed-cube"));

    // Draw some text
    if (1) {
        vx_object_t *vt = vxo_text_create(VXO_TEXT_ANCHOR_LEFT, "<<right>>hello!\n<<serif-italic-4>>line 2\nfoo<<#ff0000>>red<<sansserif-bold-30,#0000ff80>>blue semi\n<<serif-italic-4>>foo bar baz");
        vx_buffer_t *vb = vx_world_get_buffer(world, "text");
        vx_buffer_add_back(vb, vt);
        vx_buffer_swap(vb);
    }

    // Draw a custom ellipse:
    {
        int npoints = 35;
        float points[npoints*3];
        for (int i = 0; i < npoints; i++) {
            float angle = 2*M_PI*i/npoints;

            float x = 5.0f*cosf(angle);
            float y = 8.0f*sinf(angle);
            float z = 0.0f;

            points[3*i + 0] = x;
            points[3*i + 1] = y;
            points[3*i + 2] = z;
        }

        vx_buffer_add_back(vx_world_get_buffer(world, "ellipse"), vxo_lines(vx_resc_copyf (points, npoints*3),
                                                                            npoints, GL_LINE_LOOP,
                                                                            vxo_lines_style(vx_purple, 1.0f) ));
        vx_buffer_swap(vx_world_get_buffer(world, "ellipse"));
    }

    // Draw a sin wave
    {
        int npoints = 100;
        float points[npoints*3];

        for (int i = 0; i < npoints; i++) {
            float angle = 2*M_PI*i/npoints;

            float x = i*.1;
            float y = sinf(angle);
            float z = 0.0f;

            points[3*i + 0] = x;
            points[3*i + 1] = y;
            points[3*i + 2] = z;
        }

        vx_buffer_add_back(vx_world_get_buffer(world, "sin"), vxo_points(vx_resc_copyf (points, npoints*3), npoints,
                                                                             vxo_points_style(vx_purple, 10.0)));
        vx_buffer_swap(vx_world_get_buffer(world, "sin"));

    }

    // Draw a cos wave
    {
        int npoints = 100;
        float points[npoints*3];
        float colors[npoints*4];

        for (int i = 0; i < npoints; i++) {
            float angle = 2*M_PI*i/npoints;

            float x = i*.1;
            float y = cosf(angle);
            float z = 0.0f;

            points[3*i + 0] = x;
            points[3*i + 1] = y;
            points[3*i + 2] = z;

            float r = angle/(2*M_PI);
            float g = 0.3f;
            float b = 1.0f-(angle/(2*M_PI));

            colors[4*i + 0] = r;
            colors[4*i + 1] = g;
            colors[4*i + 2] = b;
            colors[4*i + 3] = 1.0f;

        }

        vx_buffer_add_back(vx_world_get_buffer(world, "cos"), vxo_points(vx_resc_copyf (points, npoints*3), npoints,
                                                                         vxo_points_style_multi_colored(vx_resc_copyf(colors, npoints*4), 10.0)));
        vx_buffer_swap(vx_world_get_buffer(world, "cos"));

    }

    // Draw a rose
    if (1) {
        int npoints = 100;
        float points[npoints*3];
        float colors[npoints*4];
        int k = 3;

        for (int i = 0; i < npoints; i++) {
            float angle = M_PI*i/npoints; // [0, Pi] for Odd

            float x = cosf(k*angle)*sin(angle);
            float y = cosf(k*angle)*cos(angle);
            float z = 0.0f;

            points[3*i + 0] = x;
            points[3*i + 1] = y;
            points[3*i + 2] = z;

            float r = angle/(M_PI);
            float g = 1.0f-(angle/(M_PI));
            float b = 0.3f;

            colors[4*i + 0] = r;
            colors[4*i + 1] = g;
            colors[4*i + 2] = b;
            colors[4*i + 3] = 1.0f;

        }

        vx_buffer_add_back(vx_world_get_buffer(world, "rose"), vxo_lines(vx_resc_copyf (points, npoints*3), npoints,
                                                                         GL_LINE_LOOP,
                                                                         vxo_lines_style_multi_colored(vx_resc_copyf(colors, npoints*4), 1.0)));
        vx_buffer_swap(vx_world_get_buffer(world, "rose"));

    }


    if (1) { // draw a box with all the fixings
        vx_buffer_t * vb = vx_world_get_buffer(world, "rect");

        // should draw purple square, with green lines, all occluded by red corners.
        vx_buffer_add_back(vb, vxo_depth_test(0,vxo_chain(
                                                  vxo_mat_translate2(-5,-5),
                                                  vxo_rect(vxo_mesh_style(vx_purple),
                                                           vxo_lines_style(vx_green, 6.0f),
                                                           vxo_points_style(vx_red, 6.0f)))));
        vx_buffer_swap(vb);

    }

    // Draw a texture
    if (state->img != NULL){
        image_u8_t * img = state->img;
        vx_object_t * o3 = vxo_image(vx_resc_copyub(img->buf, img->width*img->height*img->bpp),
                img->width, img->height, img->bpp == 4? GL_RGBA : GL_RGB, VXO_IMAGE_FLIPY);

        // pack the image into the unit square
        vx_buffer_t * vb = vx_world_get_buffer(world, "texture");
        vx_buffer_add_back(vb, vxo_chain(vxo_mat_scale3(1.0/img->width, 1.0/img->height, 1), o3));
        vx_buffer_swap(vb);
    }
}

static void display_finished(vx_application_t * app, vx_display_t * disp)
{
    state_t * state = app->impl;
    pthread_mutex_lock(&state->mutex);

    vx_layer_t * layer = NULL;

    // store a reference to the world and layer that we associate with each vx_display_t
    zhash_remove(state->layers, &disp, NULL, &layer);

    vx_layer_destroy(layer);

    // Exit after the last remote connection is closed
    if (zhash_size(state->layers) == 0) {
        state->running = 0;
    }

    pthread_mutex_unlock(&state->mutex);
}

static void display_started(vx_application_t * app, vx_display_t * disp)
{
    state_t * state = app->impl;

    {
        vx_layer_t * layer = vx_layer_create(state->world);
        vx_layer_set_display(layer, disp);
        //vx_layer_camera_op(layer, OP_PROJ_ORTHO);

        if (1) {
            float view_rel[] = {0.0, 0.0, 1.0, 1.0};
            vx_layer_set_viewport_rel(layer, view_rel);
        }

        if (1) {
            float eye3[] = {CAM_RADIUS,0,45.0f};
            float lookat3[] = {CAM_RADIUS,0,0.0f};
            float up3[] = {0,1,0};
            vx_layer_camera_lookat(layer, eye3, lookat3, up3, 0);
        }

        if (1) {
            float xy0[] = {-10,-10};
            float xy1[] = {10,10};
            vx_layer_camera_fit2D(layer, xy0, xy1, 0);
        }


        pthread_mutex_lock(&state->mutex);
        // store a reference to the world and layer that we associate with each vx_display_t
        zhash_put(state->layers, &disp, &layer, NULL, NULL);
        pthread_mutex_unlock(&state->mutex);

        // must redraw after each new display connects

        draw(state, state->world);
    }

    {
        vx_layer_t * layer = vx_layer_create(state->world2);
        vx_layer_set_display(layer, disp);
        vx_layer_camera_op(layer, OP_PROJ_ORTHO);

        {
            vx_layer_set_viewport_abs(layer, OP_ANCHOR_TOP_RIGHT, 100, 100);
        }

        {
            float eye3[] = {10.0f,10.0f,100.0f};
            float lookat3[] = {10.0f,10.0f,0.0f};
            float up3[] = {0,1,0};
            vx_layer_camera_lookat(layer, eye3, lookat3, up3, 0);
        }

        {
            float xy0[] = {-10,-10};
            float xy1[] = {10,10};
            vx_layer_camera_fit2D(layer, xy0, xy1, 0);
        }

        vx_layer_add_event_handler(layer, &state->veh);
        vx_layer_add_camera_listener(layer, &state->cl);


        /*
        pthread_mutex_lock(&state->mutex);
        // store a reference to the world and layer that we associate with each vx_display_t
        zhash_put(state->layers, &disp, &layer, NULL, NULL);
        pthread_mutex_unlock(&state->mutex);
        */

        // must redraw after each new display connects

        draw(state, state->world2);
    }


    if (0) {
        vx_layer_t * layer = vx_layer_create(state->world3);
        vx_layer_set_display(layer, disp);
        vx_layer_set_background_color(layer, vx_green);

        // Manually send the codes for animating a layer (not in
        // vx_layer_api yet):

        {
            float viewport[] = {0, 0.5, 0.5, 0.5};

            vx_code_output_stream_t * couts = vx_code_output_stream_create(128);
            couts->write_uint32(couts, OP_LAYER_VIEWPORT_REL);
            couts->write_uint32(couts, vx_layer_id(layer));
            for (int i = 0; i < 4; i++)
                couts->write_float(couts, viewport[i]);
            couts->write_uint32(couts, 2000); // animate over 2 seconds

            disp->send_codes(disp, couts->data, couts->pos);

            vx_code_output_stream_destroy(couts);
        }

        draw(state, state->world3); // XXX Why is this required to get
                                    // the layer to show up?
    }


}

static void state_destroy(state_t * state)
{

    if (state->img != NULL)
        image_u8_destroy(state->img);

    vx_world_destroy(state->world);
    vx_world_destroy(state->world2);
    vx_world_destroy(state->world3);
    assert(zhash_size(state->layers) == 0);

    zhash_destroy(state->layers);
    free(state);
}


static int touch_event (vx_event_handler_t * vh, vx_layer_t * vl, vx_camera_pos_t * pos, vx_touch_event_t * mouse)
{

    return 0;
}
static int mouse_event (vx_event_handler_t * vh, vx_layer_t * vl, vx_camera_pos_t * pos, vx_mouse_event_t * mouse)
{
    return 0;
}
static int key_event (vx_event_handler_t * vh, vx_layer_t * vl, vx_key_event_t * key)
{
    state_t *state = vh->impl;

    //printf("key pressed %d\n", key->key_code);

    if (key->key_code == 'N') {
        vx_buffer_add_back(vx_world_get_buffer(state->world2, "delayed_buffer"),
                           vxo_box(vxo_mesh_style(vx_green)));
        vx_buffer_swap(vx_world_get_buffer(state->world2, "delayed_buffer"));
        return 1;
    }
    return 0;
}

static void camera_changed (vx_camera_listener_t * cl, vx_layer_t * vl, vx_camera_pos_t * pos)
{
    printf("eye [%f,%f,%f]\n",
           pos->eye[0],pos->eye[1],pos->eye[2]);

}

static void nodestroy ()
{
    // do nothing, since this event handler is statically allocated.
}


static state_t * state_create()
{
    state_t * state = calloc(1, sizeof(state_t));
    state->running = 1;

    state->world = vx_world_create();
    state->world2 = vx_world_create();
    state->world3 = vx_world_create();
    state->layers = zhash_create(sizeof(vx_display_t*), sizeof(vx_layer_t*), zhash_ptr_hash, zhash_ptr_equals);

    // Setup event callbacks.
    {
        state->veh.dispatch_order = -10;
        state->veh.touch_event = touch_event;
        state->veh.mouse_event = mouse_event;
        state->veh.key_event = key_event;
        state->veh.destroy = nodestroy;
        state->veh.impl = state;
    }

    // Setup event callbacks.
    {
        state->cl.camera_changed = camera_changed;
        state->cl.destroy = nodestroy;
        state->cl.impl = state;
    }


    return state;
}


typedef struct {
    state_t * state;
    int id;
} tinfo_t;

void * render_loop(void * data)
{
    tinfo_t * tinfo = data;
    state_t * state = tinfo->state;
    char * buffer_name = sprintf_alloc("rot%d", tinfo->id);

    float color[4] = {1.0f, 1.0 - .1f*tinfo->id, .1f*tinfo->id, 1.0f};

    while(state->running) {
        double rad = (tinfo->id*M_PI/5) + (vx_util_mtime() % 2000) * 2* M_PI / 1e3;

        vx_object_t * vo = vxo_chain(vxo_mat_rotate_z(rad),
                                     vxo_mat_translate2(0,10),
                                     vxo_box(vxo_mesh_style(color)));

        vx_buffer_add_back(vx_world_get_buffer(state->world, buffer_name), vo);
        vx_buffer_swap(vx_world_get_buffer(state->world, buffer_name));

        usleep(500);
    }
    printf("Exiting render thread %s\n", buffer_name);
    free(buffer_name);

    return NULL;
}

void * camera_loop(void * data)
{
    state_t * state = data;

    sleep(2); // wait for 2 seconds before starting the animation

    matd_t * zaxis = matd_create(3,1);
    zaxis->data[2] = 1;

    vx_buffer_add_back(vx_world_get_buffer(state->world, "cam-circle"), vxo_chain(vxo_mat_scale(CAM_RADIUS),
                                                                                  vxo_circle(vxo_lines_style(vx_green, 3))));
    vx_buffer_swap(vx_world_get_buffer(state->world, "cam-circle"));


    int64_t start_mtime =  vx_util_mtime();
    // tell each layer to follow
    pthread_mutex_lock(&state->mutex);
    {
        zhash_iterator_t itr;
        zhash_iterator_init(state->layers, &itr);
        vx_display_t * key;
        vx_layer_t * vl;

        while(zhash_iterator_next(&itr, &key, &vl)) {
            if (1) {
                float eye3[] = {CAM_RADIUS,-CAM_RADIUS,45.0f};
                float lookat3[] = {CAM_RADIUS,0,0.0f};
                float up3[] = {0,1,0};
                vx_layer_camera_lookat(vl, eye3, lookat3, up3, 0);
            }
        }
    }
    pthread_mutex_unlock(&state->mutex);

    while (state->running) {
        // 5 seconds revolutions
        double rad = ( (vx_util_mtime() - start_mtime) % 5000) * 2* M_PI / 5e3;

        // compute the current position and orientation of the "robot"
        matd_t * orientation = matd_angle_axis_to_quat(rad, zaxis);
        matd_t * pos =  matd_create(3,1);
        pos->data[0] = cos(rad) * CAM_RADIUS;
        pos->data[1] = sin(rad) * CAM_RADIUS;

        // tell each layer to follow
        pthread_mutex_lock(&state->mutex);
        {
            zhash_iterator_t itr;
            zhash_iterator_init(state->layers, &itr);
            vx_display_t * key;
            vx_layer_t * vl;

            while(zhash_iterator_next(&itr, &key, &vl)) {
                vx_layer_camera_follow(vl, pos->data, orientation->data, 1);
            }
        }
        pthread_mutex_unlock(&state->mutex);


        vx_buffer_add_back(vx_world_get_buffer(state->world, "robot-proxy"),
                           vxo_chain(vxo_mat_quat_pos(orientation->data, pos->data),
                                     vxo_box(vxo_lines_style(vx_purple, 3))));
        vx_buffer_swap(vx_world_get_buffer(state->world, "robot-proxy"));


        matd_destroy(orientation);
        matd_destroy(pos);

        usleep(100000);
    }

    matd_destroy(zaxis);

    return NULL;
}

int main(int argc, char ** argv)
{
    getopt_t *gopt = getopt_create();
    getopt_add_bool   (gopt, 'h', "help", 0, "Show help");
    getopt_add_bool (gopt, '\0', "no-gtk", 0, "Don't show gtk window, only advertise remote connection");
    getopt_add_int (gopt, 'l', "limitKBs", "-1", "Remote display bandwidth limit. < 0: unlimited.");
    getopt_add_string (gopt, '\0', "pnm", "", "Path for pnm file to render as texture (.e.g BlockM.pnm)");
    getopt_add_bool (gopt, '\0', "stay-open", 0, "Stay open after gtk exits to continue handling remote connections");

    // parse and print help
    if (!getopt_parse(gopt, argc, argv, 1) || getopt_get_bool(gopt,"help")) {
        printf ("Usage: %s [options]\n\n", argv[0]);
        getopt_do_usage (gopt);
        exit (1);
    }

    signal(SIGPIPE, SIG_IGN); // potential fix for Valgrind "Killed" on
                              // remote viewer exit

    state_t * state = state_create();

    // Load a pnm from file, and repack the data so that it's understandable by vx
    if (strcmp(getopt_get_string(gopt,"pnm"),"")) {
        image_u8_t * img2 = image_u8_create_from_pnm(getopt_get_string(gopt, "pnm"));
        state->img = image_util_convert_rgb_to_rgba (img2);
        image_u8_destroy (img2);
    }

    vx_global_init(); // Call this to initialize the vx-wide lock. Required to start the GL thread or to use the program library

    vx_application_t app = {.impl=state, .display_started=display_started, .display_finished=display_finished};

    vx_remote_display_source_attr_t remote_attr;
    vx_remote_display_source_attr_init(&remote_attr);
    remote_attr.max_bandwidth_KBs = getopt_get_int(gopt, "limitKBs");
    remote_attr.advertise_name = "Vx Stress Test";

    vx_remote_display_source_t * cxn = vx_remote_display_source_create_attr(&app, &remote_attr);
    for (int i = 0; i < NRENDER; i++) {
        tinfo_t * tinfo = calloc(1,sizeof(tinfo_t));
        tinfo->state = state;
        tinfo->id = i;
        pthread_create(&state->render_threads[i], NULL, render_loop, tinfo);
    }

    pthread_create(&state->camera_thread, NULL, camera_loop, state);

    if (!getopt_get_bool(gopt,"no-gtk")) {
        gdk_threads_init ();
        gdk_threads_enter ();

        gtk_init (&argc, &argv);

        vx_gtk_display_source_t * appwrap = vx_gtk_display_source_create(&app);
        GtkWidget * window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
        GtkWidget * canvas = vx_gtk_display_source_get_widget(appwrap);
        gtk_window_set_default_size (GTK_WINDOW (window), 400, 400);
        gtk_container_add(GTK_CONTAINER(window), canvas);
        gtk_widget_show (window);
        gtk_widget_show (canvas); // XXX Show all causes errors!

        g_signal_connect_swapped(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);

        gtk_main (); // Blocks as long as GTK window is open
        gdk_threads_leave ();

        vx_gtk_display_source_destroy(appwrap);

        // quit when gtk closes? Or wait for remote displays/Ctrl-C
        if (!getopt_get_bool(gopt, "stay-open"))
            state->running = 0;
    }

    for (int i = 0; i < NRENDER; i++)
        pthread_join(state->render_threads[i], NULL);
    vx_remote_display_source_destroy(cxn);

    state_destroy(state);
    vx_global_destroy();
    getopt_destroy(gopt);
}
