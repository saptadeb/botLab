#include <gtk/gtk.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "vx_gtk_buffer_manager.h"

#include "vx/vx_code_input_stream.h"
#include "vx/vx_code_output_stream.h"
#include "vx/vx_codes.h"
#include "common/zhash.h"
#include "common/string_util.h"

struct vx_gtk_buffer_manager
{

    GtkWidget * window;
    GtkWidget * scroll;
    vx_display_t * disp;

    zhash_t * layers; // <layer_id, layer_info_t*>

    // See note in layout_thread() below.
    // mutex should be held for any of the above state.
    int running;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t thread;

    int layout_update_required;
};

// XXX TODOs:
//  1) Always shows up ontop of other window
//  2) While there is a scroll bar, the window does not expand to fill the screen by default currently
//  3) There's no sorting by draw order, and no way to rearrange (probably alot of work to make this right)

// We assume that a layer will be advertised before any buffers that are connected to it
// Therefore, we don't need to track which buffers are in each world, only which were swapped on a world
// after we found out that the layer existed (and which world it was attached to)

typedef struct
{
    int layer_id;
    int world_id;
    int draw_order;

    zhash_t * buffers; // <char*, buffer_info_t *>
} layer_info_t;

// pass this to the button callback so we can take the appropriate action
typedef struct
{
    int     layer_id;
    char   *name; // must free()
    int     draw_order;
    uint8_t enabled;
    vx_gtk_buffer_manager_t * man;
} buffer_info_t;

static void * layout_thread(void * data); // forward ref.

vx_gtk_buffer_manager_t * vx_gtk_buffer_manager_create(vx_display_t * disp)
{
    vx_gtk_buffer_manager_t * man  = calloc(1, sizeof(vx_gtk_buffer_manager_t));
    man->disp = disp;
    man->layers = zhash_create(sizeof(uint32_t), sizeof(layer_info_t*), zhash_uint32_hash, zhash_uint32_equals);

    pthread_cond_init(&man->cond, NULL);
    pthread_mutex_init(&man->mutex, NULL);
    pthread_create(&man->thread, NULL, layout_thread, man);

    man->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size (GTK_WINDOW (man->window), 400, 600); // tall

    man->scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(man->scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    gtk_container_add(GTK_CONTAINER(man->window), man->scroll);

    g_signal_connect_swapped(man->window,
                             "delete-event",
                             G_CALLBACK(gtk_widget_hide_on_delete),
                             GTK_WIDGET(man->window));

    return man;
}

void buffer_checkbox_changed(GtkWidget * widget, gpointer userdata)
{
    buffer_info_t * buffer = userdata;

    int enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

    vx_code_output_stream_t * outs = vx_code_output_stream_create(128);
    outs->write_uint32 (outs, OP_BUFFER_ENABLED);
    outs->write_uint32 (outs, buffer->layer_id);
    outs->write_str (outs, buffer->name);
    outs->write_uint8 (outs, enabled? 1 : 0); // convert to byte

    buffer->man->disp->send_codes(buffer->man->disp, outs->data, outs->pos);

    vx_code_output_stream_destroy(outs);
}

static int buffer_info_compare(const void * _a, const void * _b)
{
    buffer_info_t *a = *((buffer_info_t**) _a);
    buffer_info_t *b = *((buffer_info_t**) _b);

    if (a->draw_order != b->draw_order)
        return a->draw_order - b->draw_order;

    return strcmp(a->name, b->name);
}

static int layer_info_compare(const void *_a, const void *_b)
{
    layer_info_t *a = *((layer_info_t**) _a);
    layer_info_t *b = *((layer_info_t**) _b);

    if (a->draw_order != b->draw_order)
        return a->draw_order - b->draw_order;

    return a->layer_id - b->layer_id;
}

static void update_view(vx_gtk_buffer_manager_t * gtk)
{
    // This order of these two mutex locks should prevent deadloc
    // even if a user sends op codes while the user holds the GDK mutex
    gdk_threads_enter();
    pthread_mutex_lock(&gtk->mutex);

    // Clear XXX Double buffered?
    GList * children = gtk_container_get_children(GTK_CONTAINER(gtk->scroll));
    for(GList * iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    // Rebuild from scratch
    GtkWidget * box = gtk_vbox_new(0, 10);
    GtkWidget * widget = NULL;

    zarray_t *layers = zhash_values(gtk->layers); // contents: layer_info_t*
    zarray_sort(layers, layer_info_compare);

    for (int lidx = 0; lidx < zarray_size(layers); lidx++) {

        layer_info_t *linfo = NULL;
        zarray_get(layers, lidx, &linfo);

        // Draw the layer name:
        widget = gtk_label_new("");
        char * text = sprintf_alloc("<b>Layer %d</b>", linfo->layer_id);
        gtk_label_set_markup(GTK_LABEL(widget), text);
        free(text);
        //gtk_container_add(GTK_CONTAINER(box), widget);
        gtk_box_pack_start(GTK_BOX(box), widget, FALSE, FALSE, 0);

        // Make a checkbox for each buffer
        zarray_t *buffers = zhash_values(linfo->buffers); // contents: buffer_info_t*
        zarray_sort(buffers, buffer_info_compare);

        for (int i = 0; i < zarray_size(buffers); i++) {

            buffer_info_t * buffer = NULL;
            zarray_get(buffers, i, &buffer);
            assert(buffer != NULL);

            widget = gtk_check_button_new_with_label(buffer->name);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), buffer->enabled);
            g_signal_connect (G_OBJECT(widget), "toggled",
                              G_CALLBACK (buffer_checkbox_changed), buffer);
            //gtk_container_add(GTK_CONTAINER(box), widget);
            gtk_box_pack_start(GTK_BOX(box), widget, FALSE, FALSE, 0);
        }

        zarray_destroy(buffers);
    }
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(gtk->scroll), box);
    gtk_widget_show_all(gtk->scroll);
    gtk_widget_show_all(box);

    zarray_destroy(layers);

    pthread_mutex_unlock(&gtk->mutex);
    gdk_threads_leave();
}


// Note on threading, regarding 'update_view()' function.
// The GDK mutex is not recursive, and a user could potentially
// have the GDK lock locked before sending op codes. (And this
// definitely happens on initialization anyway), so we "solve/hack"
// the GDK threading requirements by creating our own thread which is
// definitely not the gdk_main(), and therefore can safely call
// gdk_threads_enter() without deadlock. -JS

static void * layout_thread(void * data)
{
    vx_gtk_buffer_manager_t * man = data;
    man->running = 1;

    while (man->running) {

        // wait for signal to redraw.
        pthread_mutex_lock(&man->mutex);
        while (!man->layout_update_required && man->running)
            pthread_cond_wait(&man->cond, &man->mutex);

        man->layout_update_required = 0;
        pthread_mutex_unlock(&man->mutex);

        if (!man->running)
            break;  // thread signaled to exit while waiting on condition

        update_view(man);

    }


    return NULL;
}

static void queue_update_view(vx_gtk_buffer_manager_t * man)
{
    pthread_mutex_lock(&man->mutex);
    man->layout_update_required = 1;
    pthread_cond_signal(&man->cond);
    pthread_mutex_unlock(&man->mutex);
}

void vx_gtk_buffer_manager_show(vx_gtk_buffer_manager_t * man, int show)
{
    if (show) {
        gtk_widget_show(man->window);
    } else {
        gtk_widget_hide(man->window);
    }
}


static buffer_info_t * ensure_buffer(vx_gtk_buffer_manager_t * man, layer_info_t *linfo,
                                   const char * name, int * created_new)
{
    assert(linfo != NULL);

    buffer_info_t * buffer = NULL;
    int success = zhash_get(linfo->buffers, &name, &buffer);

    // buffer doesn't exist yet?
    if (!success) {
        buffer = calloc(1, sizeof(buffer_info_t));
        buffer->layer_id = linfo->layer_id;
        buffer->name = strdup(name);
        buffer->enabled = 1; // default enabled
        buffer->draw_order = 10101210;// default, bogus draw order;
        buffer->man = man;

        zhash_put(linfo->buffers, &buffer->name, &buffer, NULL, NULL);

        // notify caller if we created a new buffer
        if (created_new != NULL)
            *created_new = 1;
    }

    return buffer;
}

void buffer_enabled(vx_gtk_buffer_manager_t * man, vx_code_input_stream_t * cins)
{
    int layer_id = cins->read_uint32(cins);
    const char * name = cins->read_str(cins);
    uint8_t enabled = cins->read_uint8(cins);

    int update = 0;
    pthread_mutex_lock(&man->mutex);
    {
        /* char * alloc_name = strdup(name); */
        layer_info_t * linfo = NULL;
        zhash_get(man->layers, &layer_id, &linfo);
        if (linfo == NULL) {
            pthread_mutex_unlock(&man->mutex);
            return; // XXXX assert?
        }

        int created_new = 0;
        buffer_info_t * buffer = ensure_buffer(man, linfo, name, &created_new);

        if (created_new || buffer->enabled != enabled)
        {
            buffer->enabled = enabled;
            update = 1;
        }
    } // cppcheck-ignore
    pthread_mutex_unlock(&man->mutex);

    if (update == 1)
        queue_update_view(man);


}

void process_buffer(vx_gtk_buffer_manager_t * man, vx_code_input_stream_t * cins)
{
    int world_id = cins->read_uint32(cins);
    const char * name = cins->read_str(cins);
    int draw_order = cins->read_uint32(cins);

    // find a matching layer:
    int update = 0;
    pthread_mutex_lock(&man->mutex);
    {
        layer_info_t * linfo = NULL;
        int layer_id = 0;
        zhash_iterator_t itr;
        zhash_iterator_init(man->layers, &itr);
        while(zhash_iterator_next(&itr, &layer_id, &linfo)) {
            if (linfo->world_id == world_id) {
                break;
            }
        }

        if (linfo == NULL) { // XXX Assert?
            pthread_mutex_unlock(&man->mutex);
            return;
        }

        // Now ensure there's a buffer_info_t in this layer.
        int created_new = 0;
        buffer_info_t * buffer = ensure_buffer(man, linfo, name, &created_new);

        // draw order changed?
        if (created_new || draw_order != buffer->draw_order) {
            buffer->draw_order = draw_order;
            update = 1;
        }
    } // cppcheck-ignore
    pthread_mutex_unlock(&man->mutex);

    if (update == 1)
        queue_update_view(man);
}

void process_layer(vx_gtk_buffer_manager_t * man, vx_code_input_stream_t * cins)
{
    int layer_id = cins->read_uint32(cins);
    int world_id = cins->read_uint32(cins);
    int draw_order = cins->read_uint32(cins);
    /* int bg_color = cins->read_uint32(cins); */

    int update = 0;
    pthread_mutex_lock(&man->mutex);
    {
        layer_info_t * linfo = NULL;
        int success = zhash_get(man->layers, &layer_id, &linfo);

        if (!success) {
            linfo = calloc(1,sizeof(layer_info_t));
            linfo->layer_id = layer_id;
            linfo->world_id = world_id;
            linfo->buffers = zhash_create(sizeof(char*), sizeof(buffer_info_t*), zhash_str_hash, zhash_str_equals);

            zhash_put(man->layers, &layer_id, &linfo, NULL, NULL);
            update = 1;
        }

        if (success && draw_order != linfo->draw_order) {
            assert(linfo != NULL);
            linfo->draw_order = draw_order;

            update = 1;
        }
        assert(linfo->world_id == world_id);
    }
    pthread_mutex_unlock(&man->mutex);

    if (update == 1)
        queue_update_view(man);

}


// XXX This results in us making a complete copy of the render codes...
// Maybe we can hard code the length of the buffer metadata?
void vx_gtk_buffer_manager_codes(vx_gtk_buffer_manager_t * man, const uint8_t * data, int datalen)
{
    vx_code_input_stream_t * cins = vx_code_input_stream_create(data, datalen);
    // Peek at the code type to determine which function to call
    uint32_t code = cins->read_uint32(cins);
    switch(code) {
        // only process a subset of the buffer codes
        case OP_BUFFER_ENABLED: // toggle a buffer in a particular layer
            buffer_enabled(man, cins);
            break;
        case OP_BUFFER_CODES: // when a new buffer is created on a world
            process_buffer(man, cins);
            break;
        case OP_LAYER_INFO: // what layer goes with which world
            process_layer(man, cins);
            break;
        default:
            assert(0);
    }

    vx_code_input_stream_destroy(cins);
}

static void buffer_info_destroy(buffer_info_t * binfo)
{
    free(binfo->name);
    free(binfo);
}

static void layer_info_destroy(layer_info_t * linfo)
{
    zhash_vmap_values(linfo->buffers, buffer_info_destroy);
    zhash_destroy(linfo->buffers);
    free(linfo);
}

void vx_gtk_buffer_manager_destroy(vx_gtk_buffer_manager_t * man)
{
    man->running = 0;

    pthread_mutex_lock(&man->mutex);
    pthread_cond_signal(&man->cond);
    pthread_mutex_unlock(&man->mutex);

    pthread_join(man->thread, NULL);

    zhash_vmap_values(man->layers, layer_info_destroy);
    zhash_destroy(man->layers);
    free(man);
}


