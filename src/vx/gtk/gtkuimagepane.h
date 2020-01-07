#ifndef __GTK_IMAGE_PANE_H_
#define __GTK_IMAGE_PANE_H_

#include <gtk/gtk.h>
#include <gtk/gtkwidget.h>
#include <gtk/gtkdrawingarea.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

G_BEGIN_DECLS

#define GTKU_TYPE_IMAGE_PANE            (gtku_image_pane_get_type ())
#define GTKU_IMAGE_PANE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTKU_TYPE_IMAGE_PANE, GtkuImagePane))
#define GTKU_IMAGE_PANE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GTKU_TYPE_IMAGE_PANE, GtkuImagePaneClass))
#define GTK_IS_IMAGE_PANE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTKU_TYPE_IMAGE_PANE))
#define GTK_IS_IMAGE_PANE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GTKU_TYPE_IMAGE_PANE))
#define GTKU_IMAGE_PANE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GTKU_TYPE_IMAGE_PANE, GtkuImagePaneClass))

typedef struct _GtkuImagePane        GtkuImagePane;
typedef struct _GtkuImagePaneClass   GtkuImagePaneClass;

struct _GtkuImagePane {
    GtkDrawingArea  area;
};

struct _GtkuImagePaneClass {
    GtkDrawingAreaClass parent_class;
};

GType       gtku_image_pane_get_type ();
GtkWidget * gtku_image_pane_new();
void        gtku_image_pane_set_buffer (GtkuImagePane * imgPane, GdkPixbuf * pixbuf);
int         gtku_image_pane_get_width (GtkuImagePane * imgPane);
int         gtku_image_pane_get_height (GtkuImagePane * imgPane);

G_END_DECLS

#ifdef __cplusplus
}
#endif

#endif
