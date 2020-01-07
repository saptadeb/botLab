#include "gtkuimagepane.h"
#include <assert.h>

#define GTKU_IMAGE_PANE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GTKU_TYPE_IMAGE_PANE, GtkuImagePanePrivate))
typedef struct _GtkuImagePanePrivate GtkuImagePanePrivate;
struct _GtkuImagePanePrivate {
    int width, height;
    GdkPixmap *pixMap; // server side off screen map
    GdkPixbuf *pixBuf; // client side image resource, can be NULL
};

static gboolean gtku_image_pane_expose(GtkWidget * widget, GdkEventExpose *expose);
static void gtku_image_pane_realize (GtkWidget * widget);
static void gtku_image_pane_unrealize (GtkWidget * widget);
static void gtku_image_pane_size_allocate (GtkWidget * widget,
        GtkAllocation * allocation);
static void gtku_image_pane_dispose(GObject * widget);
static void gtku_image_pane_finalize(GObject * widget);

static gboolean gtku_image_pane_enter_notify_event (GtkWidget * widget,
                                                    GdkEventCrossing * allocation);

G_DEFINE_TYPE (GtkuImagePane, gtku_image_pane, GTK_TYPE_DRAWING_AREA)

static void
gtku_image_pane_class_init (GtkuImagePaneClass * klass)
{
    GtkWidgetClass * widget_class = GTK_WIDGET_CLASS (klass);
    //GtkObjectClass * object_class = GTK_OBJECT_CLASS (klass);
    GObjectClass * gobject_class = G_OBJECT_CLASS (klass);

    widget_class->expose_event = gtku_image_pane_expose;
    widget_class->realize = gtku_image_pane_realize;
    widget_class->unrealize = gtku_image_pane_unrealize;
    widget_class->size_allocate = gtku_image_pane_size_allocate;
    widget_class->enter_notify_event = gtku_image_pane_enter_notify_event;

    //object_class->destroy = gtku_image_pane_destroy;
    gobject_class->dispose = gtku_image_pane_dispose;
    gobject_class->finalize = gtku_image_pane_finalize;

    g_type_class_add_private (gobject_class, sizeof (GtkuImagePanePrivate));

}


static void
gtku_image_pane_init (GtkuImagePane *self)
{
    GtkuImagePanePrivate * priv = GTKU_IMAGE_PANE_GET_PRIVATE (self);
    priv->width = 0;
    priv->height = 0;
    priv->pixMap = NULL;
    priv->pixBuf = NULL;

    gtk_widget_set_can_focus (GTK_WIDGET(self), TRUE);
    gtk_widget_add_events(GTK_WIDGET(self), GDK_ALL_EVENTS_MASK);
    /* check_mask(GTK_WIDGET(self)); */

    assert(gtk_widget_get_double_buffered(GTK_WIDGET(self)));
}

GtkWidget *
gtku_image_pane_new ()
{
    return GTK_WIDGET (gtk_object_new (GTKU_TYPE_IMAGE_PANE, NULL));
}

static void
gtku_image_pane_realize (GtkWidget * widget)
{
    /* chain up */
    GTK_WIDGET_CLASS (gtku_image_pane_parent_class)->realize (widget);
}

static void
gtku_image_pane_unrealize (GtkWidget * widget)
{
    /* chain up */
    GTK_WIDGET_CLASS (gtku_image_pane_parent_class)->unrealize (widget);
}


static void
gtku_image_pane_size_allocate (GtkWidget * widget,
                               GtkAllocation * allocation)
{
    GtkuImagePane * self = GTKU_IMAGE_PANE (widget);
    GtkuImagePanePrivate * priv = GTKU_IMAGE_PANE_GET_PRIVATE (self);

    /* chain up */
    GTK_WIDGET_CLASS (gtku_image_pane_parent_class)->size_allocate (widget,
            allocation);

    priv->width = allocation->width;
    priv->height = allocation->height;

}

/* Redraw the screen from the backing pixmap */
static gboolean
gtku_image_pane_expose(GtkWidget *widget, GdkEventExpose *event )
{
    GtkuImagePane * self = GTKU_IMAGE_PANE (widget);
    GtkuImagePanePrivate * priv = GTKU_IMAGE_PANE_GET_PRIVATE (self);

    int prev_width = 0, prev_height = 0;

    if (priv->pixMap != NULL) {
        gdk_pixmap_get_size(priv->pixMap, &prev_width, & prev_height);
    }

    // Reallocate the pixbuf if the window size changed since the last expose
    if (prev_width != priv->width ||
        prev_height != priv->height) {

        // create a new backing drawable, which is the correct size
        GdkPixmap * newMap = gdk_pixmap_new(widget->window,
                                            priv->width,
                                            priv->height,
                                            -1);

        // draw gray into the map if no data is present yet
        gdk_draw_rectangle (newMap,
                            widget->style->bg_gc[GTK_STATE_NORMAL],
                            TRUE,
                            0, 0,
                            widget->allocation.width,
                            widget->allocation.height);

        GdkPixmap * oldMap = priv->pixMap;
        priv->pixMap = newMap;
        if (oldMap != NULL) {
            g_object_unref(oldMap);
        }
    }

    // as soon as a pixBuf is available, draw it.
    if (priv->pixBuf != NULL) {
        gdk_draw_pixbuf(priv->pixMap,
                        NULL, // graphics context, for clipping?
                        priv->pixBuf,
                        0,0, // source in pixbuf
                        0,0, // destination in pixmap
                        -1,-1, // use full width and height
                        GDK_RGB_DITHER_NONE, 0,0);
    }

    // Finally, render the map onto the window
    gdk_draw_drawable(GDK_DRAWABLE(gtk_widget_get_window(widget)),
                      widget->style->fg_gc[GTK_WIDGET_STATE (widget)],
                      priv->pixMap,
                      event->area.x, event->area.y,
                      event->area.x, event->area.y,
                      event->area.width, event->area.height);

    return FALSE;
}


void gtku_image_pane_set_buffer (GtkuImagePane * self, GdkPixbuf * pixbuf)
{
    /* GtkuImagePane * self = GTKU_IMAGE_PANE (widget); */
    GtkWidget * widget = GTK_WIDGET(self);
    GtkuImagePanePrivate * priv = GTKU_IMAGE_PANE_GET_PRIVATE (self);

    gdk_threads_enter();

    // Draw the image onto the backing pixbuf,
    // then tell GDK it needs to redraw the whole window:

    GdkPixbuf * oldpb = priv->pixBuf;
    //1
    priv->pixBuf = pixbuf;
    if (oldpb)
        g_object_unref(oldpb);

    if (priv->pixMap == NULL)
        return; // Nothing to do


    //2
    gtk_widget_queue_draw_area (widget,
                                0, 0,
                                priv->width, priv->height);

    gdk_threads_leave();

}

int gtku_image_pane_get_width (GtkuImagePane * imgPane)
{
    return GTKU_IMAGE_PANE_GET_PRIVATE(imgPane)->width;
}

int gtku_image_pane_get_height (GtkuImagePane * imgPane)
{
    return GTKU_IMAGE_PANE_GET_PRIVATE(imgPane)->height;
}

static gboolean gtku_image_pane_enter_notify_event (GtkWidget * widget,
                                                    GdkEventCrossing * allocation)
{
    // Grab focus whenever the mouse enters the window
    gtk_widget_grab_focus (widget);
    return TRUE;
}

static void gtku_image_pane_dispose(GObject * object)
{
    // destory the pixMap and the pixBuf, on the off chance they have a reference to this object
    // Therefore other functions must be robust to seeing null pointers for pixMap and pixBuf
    GtkuImagePane * imgPane = GTKU_IMAGE_PANE(object);
    GtkuImagePanePrivate * priv = GTKU_IMAGE_PANE_GET_PRIVATE (imgPane);
    if (priv->pixMap != NULL) {
        g_object_unref(priv->pixMap);
        priv->pixMap = NULL;
    }
    if (priv->pixBuf != NULL) {
        g_object_unref(priv->pixBuf);
        priv->pixBuf = NULL;
    }

    // release any references that might have a reference to us (in this case none)
    GtkuImagePaneClass * klass = gtk_type_class(gtk_widget_get_type());
    G_OBJECT_CLASS (klass)->dispose (object);
}

static void gtku_image_pane_finalize(GObject * object)
{

    GtkuImagePaneClass * klass = gtk_type_class(gtk_widget_get_type());

    G_OBJECT_CLASS(klass)->finalize (object);
}


void gtku_image_pane_parent_set(GtkWidget * object, GtkWidget * prev_parent)
{

    GtkuImagePaneClass * klass = gtk_type_class(gtk_widget_get_type());

    GTK_WIDGET_CLASS(klass)->parent_set (object, prev_parent);
}
