#ifndef VX_GTK_DISPLAY_H
#define VX_GTK_DISPLAY_H

#include "vx/vx_display.h"
#include "gtkuimagepane.h"

vx_display_t * vx_gtk_display_create(GtkuImagePane *pane, int use_resc_mgr);
void vx_gtk_display_destroy(vx_display_t * display);

// allow events to be generated externally
int vx_gtk_display_dispatch_key(vx_display_t * state, vx_key_event_t *event);
int vx_gtk_display_dispatch_mouse(vx_display_t * state, vx_mouse_event_t *event);

void vx_gtk_display_show_context_menu(vx_display_t * disp, vx_mouse_event_t * event);

#endif
