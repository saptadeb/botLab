#include <stdarg.h>

#include "vxo_tetrahedron.h"
#include "vxo_points.h"
#include "vxo_lines.h"
#include "vxo_mesh.h"

#include "vx_global.h"
#include <math.h>

#include "vxo_chain.h"
#include "vx_codes.h"
#include "vx_program.h"
#include "vx_util.h"


#define NVERTS 4
#define NFACES 4
#define NEDGES 6

static vx_resc_t * vertices = NULL;
static vx_resc_t * tri_indices = NULL;
static vx_resc_t * line_indices = NULL;

static void vxo_tetrahedron_init()
{
    float verts_f[NVERTS*3] = {
        0, 0, sqrtf(2.0f/3),
        -1.0f/(2.0f*sqrtf(3.0f)), -1.0f/2.0f, 0,
        -1.0f/(2.0f*sqrtf(3.0f)), 1.0f/2.0f, 0,
        1.0f/sqrtf(3.0f), 0, 0,
    };


    // the 4 faces:
    uint32_t tri_ind[NFACES*3] = {
        1,3,2, // base
        0,2,3,
        0,3,1,
        0,1,2
    };

    // the 6 edges for GL_LINES
    uint32_t line_ind[NEDGES*2] =
        {
            1,2, 2,3, 3,1, // base
            0,1, 0,2, 0,3  // connect to top
        };

    vertices = vx_resc_copyf(verts_f, NVERTS*3);
    tri_indices = vx_resc_copyui(tri_ind, NFACES*3);
    line_indices = vx_resc_copyui(line_ind, NEDGES*2);

    // hold on to these references until vx_global_destroy()
    vx_resc_inc_ref(vertices);
    vx_resc_inc_ref(tri_indices);
    vx_resc_inc_ref(line_indices);
}

// will run when the program vx_global_destroy() is called by user at end of program
static void vxo_tetrahedron_destroy(void * ignored)
{
    vx_resc_dec_destroy(vertices);
    vx_resc_dec_destroy(tri_indices);
    vx_resc_dec_destroy(line_indices);
}

vx_object_t * _vxo_tetrahedron_private(vx_style_t * style, ...)
{
    // Make sure the static geometry is initialized safely, correctly,
    // and quickly
    if (vertices == NULL) {
        pthread_mutex_lock(&vx_convenience_mutex);
        if (vertices == NULL) {
            vxo_tetrahedron_init();
            vx_global_register_destroy(vxo_tetrahedron_destroy, NULL);
        }
        pthread_mutex_unlock(&vx_convenience_mutex);
    }

    vx_object_t * vc = vxo_chain_create();
    va_list va;
    va_start(va, style);
    for (vx_style_t * sty = style; sty != NULL; sty = va_arg(va, vx_style_t *)) {

        switch(sty->type) {
            case VXO_POINTS_STYLE:
                vxo_chain_add(vc, vxo_points(vertices, NVERTS, sty));
                break;
            case VXO_LINES_STYLE:
                vxo_chain_add(vc, vxo_lines_indexed(vertices, NVERTS, line_indices, GL_LINES, sty));
                break;
            case VXO_MESH_STYLE:
                // XXX normals?
                vxo_chain_add(vc, vxo_mesh_indexed(vertices, NVERTS, NULL, tri_indices, GL_TRIANGLES, sty));
                break;
        }
    }
    va_end(va);
    return vc;
}
