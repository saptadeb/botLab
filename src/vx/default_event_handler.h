#ifndef DEFAULT_EVENT_HANDLER_H
#define DEFAULT_EVENT_HANDLER_H

#include "vx_event_handler.h"
#include "default_camera_mgr.h"

#ifdef __cplusplus
extern "C" {
#endif

vx_event_handler_t * default_event_handler_create(default_cam_mgr_t * cam_mgr, uint32_t ui_animate_ms);

#ifdef __cplusplus
}
#endif

#endif
