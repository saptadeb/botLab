#include <math.h>
#include <stdarg.h>
#include <stdio.h>

#include "vxo_cylinder.h"

#include "vxo_points.h"
#include "vxo_lines.h"
#include "vxo_mesh.h"
#include "vxo_mat.h"
#include "vxo_chain.h"

#include "vx_global.h"
#include "vx_program.h"
#include "vx_codes.h"
#include "vx_util.h"

static vx_resc_t * barrel_vertices = NULL;
static vx_resc_t * barrel_normals = NULL;
static vx_resc_t * lid_vertices = NULL;
static vx_resc_t * lid_normals = NULL;
static vx_resc_t * lid_indices = NULL;

#define LID_VERTS 16


// XXX maybe split this into a function to generate your own geometry?
// right now this function should only be called once, since it sets
// static memory
static void vxo_cylinder_init(int npts)
{

    // Vertex data triangle strip for barrel and triangle fan for the top/bottom
    float barrel_verts[((npts+1) * 2)*3];
    float barrel_norms[((npts+1) * 2)*3];
    float lid_verts[(npts+2)*2]; // we'll render this twice
    float lid_norms[(npts+2)*3]; // we'll render this twice
    uint32_t lid_idxs[npts];

    // center of the lid
    lid_verts[0] = lid_verts[1] = 0.0f;
    lid_norms[0] = lid_norms[1] = 0.0f;
    lid_norms[2] = 1.0f; // points up

    for (int i = 0; i < npts + 1; i++) {
        float theta = 2*M_PI * i / npts;

        float x = 0.5f*cosf(theta);
        float y = 0.5f*sinf(theta);

        barrel_verts[i*2*3 + 0] = x;
        barrel_verts[i*2*3 + 1] = y;
        barrel_verts[i*2*3 + 2] = -0.5f;

        barrel_verts[i*2*3 + 3] = x;
        barrel_verts[i*2*3 + 4] = y;
        barrel_verts[i*2*3 + 5] = +0.5f;

        barrel_norms[i*2*3 + 0] = x;
        barrel_norms[i*2*3 + 1] = y;
        barrel_norms[i*2*3 + 2] = 0.0f;

        barrel_norms[i*2*3 + 3] = x;
        barrel_norms[i*2*3 + 4] = y;
        barrel_norms[i*2*3 + 5] = 0.0f;

        lid_verts[ 2 + i*2 + 0] = x;
        lid_verts[ 2 + i*2 + 1] = y;

        lid_norms[3 + i*3 + 0] = 0;
        lid_norms[3 + i*3 + 1] = 0.0f;
        lid_norms[3 + i*3 + 2] = 1.0f; // points up
    }

    for (int i = 0; i < npts; i++)
        lid_idxs[i] = i + 1;

    barrel_vertices = vx_resc_copyf(barrel_verts, (npts+1)*2*3);
    barrel_normals = vx_resc_copyf(barrel_norms, (npts+1)*2*3);

    lid_vertices = vx_resc_copyf(lid_verts, (npts+2)*2);
    lid_normals = vx_resc_copyf(lid_norms, (npts+2)*3);

    lid_indices = vx_resc_copyui(lid_idxs, npts);

    vx_resc_inc_ref(barrel_vertices);
    vx_resc_inc_ref(barrel_normals);
    vx_resc_inc_ref(lid_vertices);
    vx_resc_inc_ref(lid_normals);
    vx_resc_inc_ref(lid_indices);
}


// will run when the program vx_global_destroy() is called by user at end of program
static void vxo_cylinder_destroy(void * ignored)
{
    vx_resc_dec_destroy(barrel_vertices);
    vx_resc_dec_destroy(barrel_normals);
    vx_resc_dec_destroy(lid_vertices);
    vx_resc_dec_destroy(lid_normals);
    vx_resc_dec_destroy(lid_indices);
}

vx_object_t * _vxo_cylinder_private(vx_style_t * style, ...)
{
    // Make sure the static geometry is initialized safely, corcylinderly, and quickly
    if (barrel_vertices == NULL) {
        pthread_mutex_lock(&vx_convenience_mutex);
        if (barrel_vertices == NULL) {
            vxo_cylinder_init(LID_VERTS);
            vx_global_register_destroy(vxo_cylinder_destroy, NULL);
        }
        pthread_mutex_unlock(&vx_convenience_mutex);
    }


    vx_object_t * vc = vxo_chain_create();
    va_list va;
    va_start(va, style);
    for (vx_style_t * sty = style; sty != NULL; sty = va_arg(va, vx_style_t *)) {
        vx_style_inc_ref(sty);

        switch(sty->type) {
            case VXO_MESH_STYLE:
                // XXX normals?
                // barrel
                vxo_chain_add(vc, vxo_mesh(barrel_vertices,
                                           (LID_VERTS+1)*2, barrel_normals,
                                           GL_TRIANGLE_STRIP, sty));

                // bottom
                vxo_chain_add(vc, vxo_chain(vxo_mat_translate3(0,0,-0.5f),
                                            vxo_mesh(lid_vertices,
                                                     LID_VERTS+2, lid_normals,
                                                     GL_TRIANGLE_FAN, sty)));
                // top
                vxo_chain_add(vc, vxo_chain(vxo_mat_translate3(0,0,0.5f),
                                            vxo_mesh(lid_vertices,
                                                     LID_VERTS+2, lid_normals,
                                                     GL_TRIANGLE_FAN, sty)));
                break;

            case VXO_LINES_STYLE:
                // bottom
                vxo_chain_add(vc, vxo_chain(vxo_mat_translate3(0,0,-0.5f),
                                            vxo_lines_indexed(lid_vertices,
                                                              LID_VERTS, lid_indices,
                                                              GL_LINE_LOOP, sty)));

                // top
                vxo_chain_add(vc, vxo_chain(vxo_mat_translate3(0,0,0.5f),
                                            vxo_lines_indexed(lid_vertices,
                                                              LID_VERTS, lid_indices,
                                                              GL_LINE_LOOP, sty)));

                break;
            case VXO_POINTS_STYLE:
            default:
                printf("WRN: unsupported style for vxo_cylinder: 0x%x. Only mesh style is supported\n", sty->type);
        }

        vx_style_dec_destroy(sty);
    }
    va_end(va);

    return vc;


}
