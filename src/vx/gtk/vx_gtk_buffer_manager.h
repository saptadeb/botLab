#ifndef VX_GTK_BUFFER_MANAGER_H
#define VX_GTK_BUFFER_MANAGER_H

#include <stdint.h>
#include "vx/vx_display.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vx_gtk_buffer_manager vx_gtk_buffer_manager_t;

vx_gtk_buffer_manager_t * vx_gtk_buffer_manager_create(vx_display_t * display);
void vx_gtk_buffer_manager_show(vx_gtk_buffer_manager_t * bman, int show);
void vx_gtk_buffer_manager_codes(vx_gtk_buffer_manager_t * bman, const uint8_t * data, int datalen);
void vx_gtk_buffer_manager_destroy(vx_gtk_buffer_manager_t * bman);

#ifdef __cplusplus
}
#endif

#endif
