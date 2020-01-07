#include <stdlib.h>
#include <stdio.h>
#include <gtk/gtk.h>
#include "vx/vx.h"
#include "common/getopt.h"
#include <inttypes.h>

#include "vx/gtk/vx_gtk_display_source.h"
#include "vx/vx_remote_display_source.h"
#include "vx/vx_tcp_util.h"
#include "vx/vx_code_input_stream.h"

typedef struct
{
    vx_application_t app;

    vx_code_input_stream_t * codes;

} state_t;

static int verbose = 0;

static void parse_scene_codes(vx_code_input_stream_t * cins, vx_display_t * disp)
{
    int nlayers = cins->read_uint32(cins);
    if (verbose) printf("Reading %d layers\n", nlayers);
    for (int i = 0; i < nlayers; i++) {
        uint32_t length = cins->read_uint32(cins);

        const uint8_t* layer_codes = cins->read_bytes(cins, length);

        if (verbose == 2) {
            printf("Layer %d codes:\n", i);
            for (int idx = 0; idx < length; idx++) {
                printf("%02X%s",
                       layer_codes[idx],
                       (((idx+1) % 32) == 0) ? "\n" : ((((idx+1) % 4) == 0) ? " " : ""));
            }
            printf("\n");
        }

        disp->send_codes(disp, layer_codes, length);
    }

    int nworlds = cins->read_uint32(cins);
    if (verbose) printf("Reading %d worlds\n", nworlds);
    for (int i = 0; i < nworlds; i++) {
        int nbuffers = cins->read_uint32(cins);
        if (verbose) printf("  Reading %d buffers\n", nbuffers);

        for (int j = 0; j < nbuffers; j++) {
            uint32_t length = cins->read_uint32(cins);
            if (verbose) printf("  Reading buffer of length %d\n", length);

            const uint8_t* buffer_codes = cins->read_bytes(cins, length);

            if (verbose == 2) {
                printf("World %d buffer %d codes:\n", i, j);
                for (int idx = 0; idx < length; idx++) {
                    printf("%02X%s",
                           buffer_codes[idx],
                           (((idx+1) % 32) == 0) ? "\n" : ((((idx+1) % 4) == 0) ? " " : ""));
                }
                printf("\n");
            }

            disp->send_codes(disp, buffer_codes, length);
        }
    }

    if (verbose) printf("Starting to read resources\n");
    zhash_t * resources =  vx_tcp_util_unpack_resources(cins);
    disp->send_resources(disp, resources);

    zhash_destroy(resources);

    // Camera positions are optional
    if (vx_code_input_stream_available(cins) > 0 ) {
        int window_viewport[] = {cins->read_uint32(cins),
                                 cins->read_uint32(cins),
                                 cins->read_uint32(cins),
                                 cins->read_uint32(cins)};
        if (verbose)
            printf("window viewport [%d, %d, %d, %d]\n",
                   window_viewport[0], window_viewport[1], window_viewport[2], window_viewport[3]);

        int nlayers = cins->read_uint32(cins);
        for (int i = 0; i < nlayers; i++) {
            int layer_id = cins->read_uint32(cins);

            vx_camera_pos_t pos;
            vx_tcp_util_unpack_camera_pos(cins, &pos);

            int viewport[] = {cins->read_uint32(cins),
                              cins->read_uint32(cins),
                              cins->read_uint32(cins),
                              cins->read_uint32(cins)};

            if (verbose)
                printf("viewport_abs [%d, %d, %d, %d]\n", viewport[0], viewport[1], viewport[2], viewport[3]);

            float viewport_rel[] = { ((float) viewport[0]) / ((float) window_viewport[2]),
                                     ((float) viewport[1]) / ((float) window_viewport[3]),
                                     ((float) viewport[2]) / ((float) window_viewport[2]),
                                     ((float) viewport[3]) / ((float) window_viewport[3]) };

            if (verbose)
                printf("viewport_rel [%5.1f, %5.1f, %5.1f, %5.1f]\n",
                       viewport_rel[0], viewport_rel[1], viewport_rel[2], viewport_rel[3]);

            // set viewport
            {
                vx_code_output_stream_t * couts = vx_code_output_stream_create(128);
                couts->write_uint32(couts, OP_LAYER_VIEWPORT_REL);
                couts->write_uint32(couts, layer_id);

                couts->write_float(couts, viewport_rel[0]);
                couts->write_float(couts, viewport_rel[1]);
                couts->write_float(couts, viewport_rel[2]);
                couts->write_float(couts, viewport_rel[3]);

                couts->write_uint32(couts, 0);

                disp->send_codes(disp, couts->data, couts->pos);
                vx_code_output_stream_destroy(couts);
            }

            // set camera
            {
                vx_code_output_stream_t * couts = vx_code_output_stream_create(128);
                couts->write_uint32(couts, OP_LAYER_CAMERA);
                couts->write_uint32(couts, layer_id);
                couts->write_uint32(couts, OP_LOOKAT);

                couts->write_float(couts, pos.eye[0]);
                couts->write_float(couts, pos.eye[1]);
                couts->write_float(couts, pos.eye[2]);

                couts->write_float(couts, pos.lookat[0]);
                couts->write_float(couts, pos.lookat[1]);
                couts->write_float(couts, pos.lookat[2]);

                couts->write_float(couts, pos.up[0]);
                couts->write_float(couts, pos.up[1]);
                couts->write_float(couts, pos.up[2]);

                couts->write_uint32(couts, 0);
                couts->write_uint8(couts, 1);

                disp->send_codes(disp, couts->data, couts->pos);
                vx_code_output_stream_destroy(couts);
            }
        }
    }
}

static void display_finished(vx_application_t * app, vx_display_t * disp)
{
    // Do nothing?
}

static void display_started(vx_application_t * app, vx_display_t * disp)
{
    state_t * state = app->impl;
    parse_scene_codes(state->codes, disp);

    // replay saved state
}

void do_usage_and_exit(getopt_t *gopt, int argc, char ** argv)
{
    printf ("Usage: %s [options] <scene>\n\n", argv[0]);
    getopt_do_usage (gopt);
    exit (1);
}

int main(int argc, char ** argv)
{
    getopt_t *gopt = getopt_create();
    getopt_add_bool   (gopt, 'h', "help", 0, "Show help");
    getopt_add_bool (gopt, '\0', "no-gtk", 0, "Don't show gtk window, only advertise remote connection");
    getopt_add_bool (gopt, '\0', "stay-open", 0, "Stay open after gtk exits to continue handling remote connections");

    // parse and print help
    if (!getopt_parse(gopt, argc, argv, 1) || getopt_get_bool(gopt,"help")) {
        do_usage_and_exit(gopt, argc, argv);
    }

    vx_global_init(); // Call this to initialize the vx-wide lock. Required to start the GL thread or to use the program library

    state_t * state = calloc(1, sizeof(state_t));
    state->app.impl=state;
    state->app.display_started=display_started;
    state->app.display_finished=display_finished;

    const zarray_t *scenes = getopt_get_extra_args(gopt);
    if (zarray_size(scenes) == 0)
        do_usage_and_exit(gopt, argc, argv);

    if (1) {

        char *scene_path;
        zarray_get(scenes, 0, &scene_path);

        FILE * fp = fopen(scene_path, "r");
        if (fp == NULL) {
            printf("ERR: Unable to read %s\n", scene_path);
            exit(1);
        }

        fseek(fp, 0, SEEK_END);
        uint64_t length = ftell(fp);
        rewind(fp);
        uint8_t * filedata = malloc(length);
        uint64_t rcount = fread(filedata, 1, length, fp);
        if (rcount != length) {
            printf("Failed to read from %s. Expected %"PRIu64" but only got %"PRIu64"\n",
                   scene_path, length, rcount);
        }
        fclose(fp);

        state->codes = vx_code_input_stream_create(filedata, length);

        free(filedata);


        printf("Read %"PRIu64" bytes from %s\n", length, scene_path);
    }

    vx_remote_display_source_t * cxn = vx_remote_display_source_create(&state->app);

    if (!getopt_get_bool(gopt,"no-gtk")) {
        gdk_threads_init ();
        gdk_threads_enter ();

        gtk_init (&argc, &argv);

        vx_gtk_display_source_t * appwrap = vx_gtk_display_source_create(&state->app);
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

    }
    vx_remote_display_source_destroy(cxn);

    vx_global_destroy();
    getopt_destroy(gopt);
    free(state);

}
