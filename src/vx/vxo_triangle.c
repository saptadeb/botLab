#include <stdarg.h>

#include "vxo_triangle.h"
#include <math.h>

#include "vx_codes.h"
#include "vx_global.h"


#include "vxo_chain.h"
#include "vxo_mesh.h"
#include "vxo_points.h"
#include "vxo_lines.h"
#include "vxo_mat.h"

#define NVERTS 3
#define NIDX 3
static vx_resc_t * points = NULL;
static vx_resc_t * normals = NULL;
static vx_resc_t * indices = NULL;

static void vxo_triangle_init()
{
    /* float verts_f[2*3] = {-0.5f, 0.0f, */
    /*                        0.5f, 0.0f, */
    /*                        0.0f, VXO_TRIANGLE_EQUILATERAL_ALT}; */

    double long_edge = cos(30 * M_PI/180) /2;
    double short_edge = sin(30 * M_PI/180) /2;

    float verts_f[2*3] = {-short_edge, -long_edge,
                           -short_edge, long_edge,
                          long_edge, 0.0f };

    uint32_t ind_i[3] = {0,1,2};
    float norms_f[3*3] = {0,0,1,
                          0,0,1,
                          0,0,1};

    points = vx_resc_copyf(verts_f, NVERTS*2);
    normals = vx_resc_copyf(norms_f, NVERTS*3);
    indices = vx_resc_copyui(ind_i, NIDX);

    vx_resc_inc_ref(points); // hold on to these references until vx_global_destroy()
    vx_resc_inc_ref(indices);
    vx_resc_inc_ref(normals);
}

// will run when the program vx_global_destroy() is called by user at end of program
static void vxo_triangle_destroy(void * ignored)
{
    vx_resc_dec_destroy(points);
    vx_resc_dec_destroy(indices);
    vx_resc_dec_destroy(normals);
}

vx_object_t * _vxo_triangle_private(vx_style_t * style, ...)
{
    // Make sure the static geometry is initialized safely, correctly, and quickly
    if (points == NULL) {
        pthread_mutex_lock(&vx_convenience_mutex);
        if (points == NULL) {
            vxo_triangle_init();
            vx_global_register_destroy(vxo_triangle_destroy, NULL);
        }
        pthread_mutex_unlock(&vx_convenience_mutex);
    }

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
                vxo_chain_add(vc, vxo_mesh_indexed(points, NVERTS, normals, indices, GL_TRIANGLES, sty));
                break;
        }
    }
    va_end(va);

    return vc;
}
