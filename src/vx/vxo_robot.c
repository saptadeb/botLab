#include <stdarg.h>

#include "vxo_robot.h"

#include "vx_global.h"
#include "vxo_points.h"
#include "vxo_lines.h"
#include "vxo_mesh.h"

#include "vx_codes.h"
#include "vxo_chain.h"
#include "vxo_mat.h"

#define DEFAULT_LENGTH 1.0f
#define DEFAULT_WIDTH  0.45f

#define NVERTS 3
#define NIDX 3
static vx_resc_t * points = NULL;

// makes a unit width and length robot
static void vxo_robot_init()
{
    float verts_f[NVERTS*2] = { -DEFAULT_LENGTH/2,   DEFAULT_WIDTH/2,
                                -DEFAULT_LENGTH/2,  -DEFAULT_WIDTH/2,
                                DEFAULT_LENGTH/2,  0.0f
    };

    points = vx_resc_copyf(verts_f, NVERTS*2);

    vx_resc_inc_ref(points); // hold on to these forever
}

// will run when the program vx_global_destroy() is called by user at
// the end of program
static void vxo_robot_destroy(void * ignored)
{
    vx_resc_dec_destroy(points);
}

vx_object_t * _vxo_robot_private(vx_style_t * style, ...)
{
    // Make sure the static geometry is initialized safely, correctly, and quickly
    if (points == NULL) {
        pthread_mutex_lock(&vx_convenience_mutex);
        if (points == NULL) {
            vxo_robot_init();
            vx_global_register_destroy(vxo_robot_destroy, NULL);
        }
        pthread_mutex_unlock(&vx_convenience_mutex);
    }

    assert(points != NULL);
    vx_object_t * vc = vxo_chain_create();

    va_list va;
    va_start(va, style);
    for (vx_style_t * sty = style; sty != NULL; sty = va_arg(va, vx_style_t *)) {

        switch(sty->type) {
            case VXO_POINTS_STYLE:
                vxo_chain_add(vc, vxo_points(points, NVERTS, sty));
                break;
            case VXO_LINES_STYLE:
                vxo_chain_add(vc, vxo_lines(points, NVERTS, GL_LINE_LOOP, sty));
                break;
            case VXO_MESH_STYLE:
                // XXX normals?
                vxo_chain_add(vc, vxo_mesh(points, NVERTS, NULL, GL_TRIANGLES, sty));
                break;
        }
    }
    va_end(va);

    return vc;
}