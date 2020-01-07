#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <gtk/gtk.h>

// core api
#include "vx/vx.h"
#include "vx/vx_util.h"
#include "vx/gtk/vx_gtk_display_source.h"

#include "common/getopt.h"
#include "common/pg.h"
#include "common/zarray.h"

#include "vx/math/math_util.h"

#include "imagesource/image_u32.h"

#include "vx_utils.hpp"

struct app_default_implementation {
    vx_world_t *world;
    zhash_t *layers;
    vx_event_handler_t *vxeh;
    pthread_mutex_t mutex;
};

app_default_implementation_t *
app_default_implementation_create (vx_world_t *world, vx_event_handler_t *vxeh)
{
    app_default_implementation_t *impl = new app_default_implementation_t;
    impl->world = world;
    impl->vxeh = vxeh;
    impl->layers = zhash_create (sizeof(vx_display_t *), sizeof(vx_layer_t *),
                                 zhash_ptr_hash, zhash_ptr_equals);

    pthread_mutex_init (&impl->mutex, NULL);

    return impl;
}

void
app_default_display_started (vx_application_t *app, vx_display_t *disp)
{
    app_default_implementation_t *impl = static_cast<app_default_implementation_t*>(app->impl);

    vx_layer_t *layer = vx_layer_create (impl->world);
    vx_layer_set_background_color (layer, vx_white);
    vx_layer_set_display (layer, disp);

    pthread_mutex_lock (&impl->mutex);
    // store a reference to the world and layer that we associate with each vx_display_t
    zhash_put (impl->layers, &disp, &layer, NULL, NULL);
    pthread_mutex_unlock (&impl->mutex);

    if (impl->vxeh != NULL)
        vx_layer_add_event_handler (layer, impl->vxeh);

    vx_layer_camera_op (layer, OP_PROJ_PERSPECTIVE);
    float eye[3]    = {  0,  0,  1 };
    float lookat[3] = {  0,  0,  0 };
    float up[3]     = {  0,  1,  0 };
    vx_layer_camera_lookat (layer, eye, lookat, up, 1);

    vx_code_output_stream_t *couts = vx_code_output_stream_create (128);
    couts->write_uint32 (couts, OP_LAYER_CAMERA);
    couts->write_uint32 (couts, vx_layer_id (layer));
    couts->write_uint32 (couts, OP_INTERFACE_MODE);
    couts->write_float  (couts, 2.5f);
    disp->send_codes (disp, couts->data, couts->pos);
    vx_code_output_stream_destroy (couts);
}

void
app_default_display_finished (vx_application_t *app, vx_display_t *disp)
{
    app_default_implementation_t *impl = static_cast<app_default_implementation_t*>(app->impl);
    pthread_mutex_lock (&impl->mutex);

    vx_layer_t *layer = NULL;
    zhash_remove (impl->layers, &disp, NULL, &layer);
    vx_layer_destroy (layer);

    pthread_mutex_unlock (&impl->mutex);
}

void
app_init (int argc, char *argv[])
{
    // on newer GTK systems, this might generate an error/warning
    g_type_init ();

    // Secure glib
    if (!g_thread_supported ())
        g_thread_init (NULL);

    // Initialize GTK
    gdk_threads_init ();
    gdk_threads_enter ();
    gtk_init (&argc, &argv);

    vx_global_init ();
}

void
app_gui_run (vx_application_t *app, parameter_gui_t *pg, int w, int h)
{
    // Creates a GTK window to wrap around our vx display canvas. The vx world
    // is rendered to the canvas widget, which acts as a viewport into your
    // virtual world.
    vx_gtk_display_source_t *appwrap = vx_gtk_display_source_create (app);
    GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    GtkWidget *canvas = vx_gtk_display_source_get_widget (appwrap);
    GtkWidget *pgui = pg_get_widget (pg);
    gtk_window_set_default_size (GTK_WINDOW (window), w, h);

    // Pack a parameter gui and canvas into a vertical box
    GtkWidget *vbox = gtk_vbox_new (0, 0);
    gtk_box_pack_start (GTK_BOX (vbox), canvas, 1, 1, 0);
    gtk_widget_show (canvas);    // XXX Show all causes errors!
    gtk_box_pack_start (GTK_BOX (vbox), pgui, 0, 0, 0);
    gtk_widget_show (pgui);

    gtk_container_add (GTK_CONTAINER (window), vbox);
    gtk_widget_show (window);
    gtk_widget_show (vbox);

    g_signal_connect_swapped (G_OBJECT (window), "destroy", G_CALLBACK (gtk_main_quit), NULL);

    gtk_main (); // Blocks as long as GTK window is open
    gdk_threads_leave ();

    // destroy function was causing segfault, so comment it out for now
    //vx_gtk_display_source_destroy (appwrap);
}

int
app_set_camera (vx_application_t *app, const float eye[3], const float lookat[3], const float up[3])
{
    int set = 0;
    app_default_implementation_t *impl = static_cast<app_default_implementation_t*>(app->impl);
    pthread_mutex_lock (&impl->mutex);
    {
        zhash_iterator_t zit;
        zhash_iterator_init (impl->layers, &zit);
        vx_layer_t *layer = NULL;
        while (zhash_iterator_next (&zit, NULL, &layer)) {
            vx_layer_camera_lookat (layer, eye, lookat, up, 1);
            set++;
        }
    }
    pthread_mutex_unlock (&impl->mutex);
    return set;
}
