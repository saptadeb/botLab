#ifndef VX_TYPES_H
#define VX_TYPES_H

// Centralized forward references and typedefs for the core vx types
// to avoid multiple typedef warnings on GCC 4.3

struct vx_layer;
typedef struct vx_layer vx_layer_t;

struct vx_renderer;
typedef struct vx_renderer vx_renderer_t;

struct vx_event_handler;
typedef struct vx_event_handler vx_event_handler_t;

struct vx_camera_listener;
typedef struct vx_camera_listener vx_camera_listener_t;

struct vx_key_event;
typedef struct vx_key_event vx_key_event_t;

struct vx_mouse_event;
typedef struct vx_mouse_event vx_mouse_event_t;

struct vx_touch_event;
typedef struct vx_touch_event vx_touch_event_t;

struct vx_world;
typedef struct vx_world vx_world_t;

struct vx_buffer;
typedef struct vx_buffer vx_buffer_t;

struct vx_object;
typedef struct vx_object vx_object_t;

struct vx_resc;
typedef struct vx_resc vx_resc_t;

struct vx_camera_pos;
typedef struct vx_camera_pos vx_camera_pos_t;

struct vx_camera_mgr;
typedef struct vx_camera_mgr vx_camera_mgr_t;


#endif
