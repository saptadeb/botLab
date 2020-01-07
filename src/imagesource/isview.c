#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "common/getopt.h"

#include "image_u8x3.h"
#include "image_source.h"
#include "image_convert.h"

typedef struct state state_t;
struct state {
    char     *url; // image_source url
    image_source_t *isrc;
    int fidx;

    getopt_t *gopt;

    pthread_t runthread;

    GtkWidget *window;
    GtkWidget *image;

    pthread_mutex_t mutex;

    FILE *record_islog;
};

static int
write_u32 (FILE *f, uint32_t v)
{
    uint8_t buf[4];
    buf[0] = (v>>24) & 0xff;
    buf[1] = (v>>16) & 0xff;
    buf[2] = (v>>8) & 0xff;
    buf[3] = (v>>0) & 0xff;

    int res = fwrite(buf, 1, 4, f);
    if (res != 4)
        return -1;
    return 0;
}

static int
write_u64 (FILE *f, uint64_t v)
{
    if (write_u32(f, (v>>32)) || write_u32(f, v & 0xffffffff))
        return -1;

    return 0;
}

void
my_gdkpixbufdestroy (guchar *pixels, gpointer data)
{
    free (pixels);
}

gint
callback_func (GtkWidget *widget, GdkEventKey *event, gpointer callback_data)
{
    state_t *state = (state_t*) callback_data;
    image_source_t *isrc = state->isrc;

    switch (event->keyval) {
        case GDK_KEY_Tab: {
            printf("tab\n");
            state->fidx = (state->fidx + 1) % isrc->num_formats(isrc);
            pthread_mutex_lock(&state->mutex);
            isrc->stop(isrc);
            isrc->set_format(isrc, state->fidx);
            printf("set format %d\n", state->fidx);
            isrc->start(isrc);
            pthread_mutex_unlock(&state->mutex);
            break;
        }

        case GDK_KEY_r:
        case GDK_KEY_R: {
            if (state->record_islog != NULL) {
                fclose(state->record_islog);
                state->record_islog = NULL;
                printf("islog recording stopped\n");
            } else {
                state->record_islog = fopen("/tmp/isview.islog", "w");
                printf("islog recording started\n");
            }
            break;
        }
    }
    printf("got callback\n");
    return 0;
}

void *
runthread (void *_p)
{
    state_t *state = (state_t*) _p;
    image_source_t *isrc = state->isrc;

    while (1) {

        image_source_data_t isdata;
        image_u8x3_t *im = NULL;

        pthread_mutex_lock(&state->mutex);
        int res = isrc->get_frame(isrc, &isdata);
        if (!res) {
            im = image_convert_u8x3(&isdata);

            if (state->record_islog) {
                write_u64(state->record_islog, 0x17923349ab10ea9aUL);
                write_u64(state->record_islog, isdata.utime);
                write_u32(state->record_islog, isdata.ifmt.width);
                write_u32(state->record_islog, isdata.ifmt.height);
                int formatlen = strlen(isdata.ifmt.format);
                write_u32(state->record_islog, formatlen);
                fwrite(isdata.ifmt.format, 1, formatlen, state->record_islog);
                write_u32(state->record_islog, isdata.datalen);
                fwrite(isdata.data, 1, isdata.datalen, state->record_islog);
                fflush(state->record_islog);
            }
        }

        isrc->release_frame(isrc, &isdata);
        pthread_mutex_unlock(&state->mutex);

        if (res)
            goto error;

        if (im != NULL) {
            gdk_threads_enter();

            GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data((guchar*) im->buf,
                                                         GDK_COLORSPACE_RGB, 0, 8,
                                                         im->width, im->height, im->stride,
                                                         my_gdkpixbufdestroy,
                                                         NULL);


            gtk_image_set_from_pixbuf(GTK_IMAGE(state->image), pixbuf);
            g_object_unref(G_OBJECT(pixbuf));

            gdk_threads_leave();


        }

        usleep(0);
    }

  error:
    isrc->stop(isrc);
    printf("exiting\n");

    return NULL;
}

int
main (int argc, char *argv[] )
{
    state_t *state = calloc(1, sizeof(*state));
    state->gopt = getopt_create();

    getopt_add_bool(state->gopt, 'h', "--help", 0, "Show this help");

    if (!getopt_parse(state->gopt, argc, argv, 0)) {
        getopt_do_usage(state->gopt);
        exit(-1);
    }

    const zarray_t *args = getopt_get_extra_args(state->gopt);
    if (zarray_size(args) > 0) {
        zarray_get(args, 0, &state->url);
    } else {
        zarray_t *urls = image_source_enumerate();

        printf("Cameras:\n");
        for (int i = 0; i < zarray_size(urls); i++) {
            char *url;
            zarray_get(urls, i, &url);
            printf("  %3d: %s\n", i, url);
        }

        if (zarray_size(urls) == 0) {
            printf("No cameras found.\n");
            exit(0);
        }
        zarray_get(urls, 0, &state->url);
    }

    g_type_init();
    gtk_init (&argc, &argv);
    gdk_threads_init();

    state->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(state->window), state->url);

    pthread_mutex_init(&state->mutex, NULL);
    state->image = gtk_image_new();

    gtk_container_add(GTK_CONTAINER(state->window), state->image);
    gtk_widget_show(state->image);
    gtk_widget_show(state->window);

    g_signal_connect(state->window, "key_press_event",
                     G_CALLBACK(callback_func), state);

    //////////////////////////////////////////////////////////
    state->isrc = image_source_open(state->url);
    if (state->isrc == NULL) {
        printf("Unable to open device %s\n", state->url);
        exit(-1);
    }

    image_source_t *isrc = state->isrc;

    if (isrc->start(isrc))
        exit(-1);

    state->fidx = isrc->get_current_format(isrc);

    printf("Image source formats:\n");
    for (int i = 0; i < isrc->num_formats(isrc); i++) {
        image_source_format_t ifmt;
        isrc->get_format(isrc, i, &ifmt);
        printf("\t%d\t%4d x %4d (%s)\n", i, ifmt.width, ifmt.height, ifmt.format);
    }

    printf("Image source features:\n");
    for (int i = 0; i < isrc->num_features(isrc); i++) {
        const char *feature_name = isrc->get_feature_name(isrc, i);
        char *feature_type = isrc->get_feature_type(isrc, i);
        double v = isrc->get_feature_value(isrc, i);

        printf("\t%-20s %10f     %s\n", feature_name, v, feature_type);
        free(feature_type);
    }

    pthread_create(&state->runthread, NULL, runthread, state);

    gtk_main();

    return 0;
}
