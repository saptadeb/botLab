#include <gtk/gtk.h>
#include <math.h>

#include "vx/vxo_drawables.h"

#include "vx/gtk/vx_gtk_display_source.h"

// core api
#include "vx/vx_global.h"
#include "vx/vx_layer.h"
#include "vx/vx_world.h"
#include "vx/vx_colors.h"

typedef struct
{
    vx_object_t * obj;
    char * name;
} obj_data_t;


typedef struct
{
    zarray_t * obj_data;
    vx_world_t * world;
} state_t;

static void draw(vx_world_t * world, zarray_t * obj_data);
static void display_finished(vx_application_t * app, vx_display_t * disp);
static void display_started(vx_application_t * app, vx_display_t * disp);

// Convenience macro to allow easily adding new vxo objects to the zoo
#define ADD_OBJECT(s, call)                                          \
    {                                                               \
        obj_data_t data = {.obj = s call, .name = #s};   \
        zarray_add(state.obj_data, &data);                          \
    }                                                               \


int main(int argc, char ** argv)
{
    vx_global_init();

    state_t state;

    state.world =  vx_world_create();
    state.obj_data = zarray_create(sizeof(obj_data_t));

    ADD_OBJECT(vxo_rect, (vxo_mesh_style(vx_blue),
                          vxo_points_style(vx_orange, 10.0f)));

    ADD_OBJECT(vxo_box, (vxo_mesh_style(vx_blue)));
    ADD_OBJECT(vxo_circle, (vxo_mesh_style(vx_orange)));
    ADD_OBJECT(vxo_robot, (vxo_mesh_style(vx_green),
                           vxo_lines_style(vx_white, 2.0f)));
    ADD_OBJECT(vxo_tetrahedron, (vxo_lines_style(vx_green, 2.0f)));
    ADD_OBJECT(vxo_square_pyramid, (vxo_lines_style(vx_red, 2.0f)));
    ADD_OBJECT(vxo_cylinder, (vxo_mesh_style(vx_red)));
    ADD_OBJECT(vxo_axes, ());
    ADD_OBJECT(vxo_triangle, (vxo_mesh_style(vx_green)));
    ADD_OBJECT(vxo_sphere, (vxo_mesh_style_fancy(vx_red, vx_orange, vx_white,
                                                 1.0, 30, 2)));

    vx_application_t app = {.impl=&state, .display_started=display_started, .display_finished=display_finished};

    gdk_threads_init ();
    gdk_threads_enter ();

    gtk_init (&argc, &argv);

    vx_gtk_display_source_t * appwrap = vx_gtk_display_source_create(&app);
    GtkWidget * window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    GtkWidget * canvas = vx_gtk_display_source_get_widget(appwrap);
    gtk_window_set_default_size (GTK_WINDOW (window), 1280, 720);
    gtk_container_add(GTK_CONTAINER(window), canvas);
    gtk_widget_show (window);
    gtk_widget_show (canvas); // XXX Show all causes errors!

    g_signal_connect_swapped(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_main (); // Blocks as long as GTK window is open
    gdk_threads_leave ();

    vx_gtk_display_source_destroy(appwrap);
    vx_global_destroy();
}

static void draw(vx_world_t * world, zarray_t * obj_data)
{
    vx_buffer_t * vb = vx_world_get_buffer(world, "zoo");

    int cols = sqrt(zarray_size(obj_data)) + 1;
    int rows = zarray_size(obj_data)/cols + 1;
    int grid = 4;

    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            int idx = y*cols + x;
            if (idx >= zarray_size(obj_data))
                break;

            obj_data_t  data;
            zarray_get(obj_data, idx, &data);
            vx_object_t * vo = data.obj;

            vx_buffer_add_back(vb, vxo_chain(vxo_mat_translate2(x*grid + grid/2, rows*grid - (y*grid +grid/2)),
                                             vxo_chain(vxo_mat_scale(grid),
                                                       vxo_rect(vxo_lines_style(vx_gray, 2))),
                                             vo));
            // XXX Text, box outlines
        }
    }
    vx_buffer_swap(vb); // XXX Could name buffers by object name
}

static void display_finished(vx_application_t * app, vx_display_t * disp)
{
    // XXX layer leak
}

static void display_started(vx_application_t * app, vx_display_t * disp)
{
    state_t * state = app->impl;

    vx_layer_t * layer = vx_layer_create(state->world);
    vx_layer_set_display(layer, disp);
    vx_layer_set_background_color(layer, vx_black);
    // XXX bug in world
    draw(state->world, state->obj_data);
}

