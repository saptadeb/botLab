#ifndef VX_TCP_DISPLAY_H
#define VX_TCP_DISPLAY_H

#include "vx_display.h"
#include "common/ssocket.h"


// intentionally incompatible with vx_tcp_renderer
#define VX_TCP_ADD_RESOURCES  0x111
#define VX_TCP_CODES          0x222
#define VX_TCP_VIEWPORT_SIZE  0x333
#define VX_TCP_EVENT_KEY      0x444
#define VX_TCP_EVENT_MOUSE    0x445
#define VX_TCP_EVENT_TOUCH    0x446
#define VX_TCP_CAMERA_CHANGED 0x447


#ifdef __cplusplus
extern "C" {
#endif

vx_display_t * vx_tcp_display_create(ssocket_t * cxn, int limitKBs, void (*cxn_closed_callback)(vx_display_t * disp, void * cpriv), void * cpriv);
void vx_tcp_display_destroy(vx_display_t * disp);

// Call this function to initiate a graceful shutdown of this display.
// First closes the tcp stream, which then results in the closed callback being called, where destroy(disp) should be called
void vx_tcp_display_close(vx_display_t * disp);

#ifdef __cplusplus
}
#endif

#endif
