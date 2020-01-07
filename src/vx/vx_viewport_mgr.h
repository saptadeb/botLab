#ifndef VX_VIEWPORT_MGR_H
#define VX_VIEWPORT_MGR_H

// Class used for managing viewport animation
// Not threadsafe, so ensure externally that all calls are protected by
// a mutex

typedef struct vx_viewport_mgr vx_viewport_mgr_t;

vx_viewport_mgr_t * vx_viewport_mgr_create(void);

// Pass in the window size and current time, returns the viewport in
// pixels for this layer, which the caller is responsible for freeing
int * vx_viewport_mgr_get_pos(vx_viewport_mgr_t *, int *fullviewport4, uint64_t mtime);


void vx_viewport_mgr_set_rel(vx_viewport_mgr_t *, float * viewport4_rel, uint64_t mtime_goal);

void vx_viewport_mgr_set_abs(vx_viewport_mgr_t *, int align_type,
                             int offx, int offy,
                             int width, int height,
                             uint64_t mtime_goal);

void vx_viewport_mgr_destroy(vx_viewport_mgr_t *);


#endif
