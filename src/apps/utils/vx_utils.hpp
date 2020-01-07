#ifndef APPS_UTILS_VX_UTILS_H
#define APPS_UTILS_VX_UTILS_H

#include "vx/vx.h"
#include "common/pg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct app_default_implementation app_default_implementation_t;

app_default_implementation_t *
app_default_implementation_create (vx_world_t *world, vx_event_handler_t *vxeh);

void
app_default_display_started (vx_application_t *app, vx_display_t *disp);

void
app_default_display_finished (vx_application_t *app, vx_display_t *disp);

void
app_init (int argc, char *argv[]);

void
app_gui_run (vx_application_t *app, parameter_gui_t *pg, int w, int h);

int
app_set_camera (vx_application_t *app, const float eye[3], const float lookat[3], const float up[3]);

#ifdef __cplusplus
}
#endif

#endif // APPS_UTILS_VX_UTILS_H
