#ifndef VX_GTK_DISPLAY_SOURCE_H
#define VX_GTK_DISPLAY_SOURCE_H

#include "vx/vx_display.h"
#include <gtk/gtkwidget.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vx_gtk_display_source vx_gtk_display_source_t;

// Create a new connection manager, with a callback when a new connection is established
// User should free/manager *app after this call returns
vx_gtk_display_source_t * vx_gtk_display_source_create_toggle_mgr(vx_application_t * app, int use_resc_mgr);
vx_gtk_display_source_t * vx_gtk_display_source_create(vx_application_t * app);
GtkWidget * vx_gtk_display_source_get_widget(vx_gtk_display_source_t * disp);

void vx_gtk_display_source_destroy(vx_gtk_display_source_t* gtk_wrapper);

#ifdef __cplusplus
}
#endif

#endif
