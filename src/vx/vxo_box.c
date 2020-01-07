#include <stdarg.h>

#include "vx_codes.h"
#include "vx_global.h"

#include "vxo_points.h"
#include "vxo_lines.h"
#include "vxo_mesh.h"


#include "vxo_box.h"
#include "vx_program.h"
#include "vx_util.h"
#include "vxo_chain.h"
#include "vxo_mat.h"

#define NVERTS 8

//6 faces, 2 triangles, 3 vertices
#define N_TRI_VERT 36
#define N_LINE_IDX 12
static vx_resc_t * vertex_points = NULL;
static vx_resc_t * tri_points = NULL;
static vx_resc_t * tri_normals = NULL;
static vx_resc_t * line_indices = NULL;

static void vxo_box_init()
{
    // for GL_POINTS
    float point_verts[NVERTS*3] =
        {
            -0.5f,-0.5f,-0.5f, // 0
            -0.5f,+0.5f,-0.5f, // 1
            +0.5f,+0.5f,-0.5f, // 2
            +0.5f,-0.5f,-0.5f, // 3
            -0.5f,-0.5f,+0.5f, // 4
            -0.5f,+0.5f,+0.5f, // 5
            +0.5f,+0.5f,+0.5f, // 6
            +0.5f,-0.5f,+0.5f, // 7
        };

    // for GL_LINES
    uint32_t line_idxs[N_LINE_IDX*2] =
        {
            0,1, 1,2, 2,3, 3,0, // bottom
            4,5, 5,6, 6,7, 7,4, // top
            0,4, 1,5, 2,6, 3,7  // sides
        };

    // for GL_TRIANGLES
    // Note: we could reduce the amount of vertex data
    // by using indices -- e.g. point 0 and 2 are listed twice with the
    // same normal on the bottom face (would save us 16 vertices out of 48)
    // each vertex is listed 6 times, has 3 coords
    float tri_verts[N_TRI_VERT*3] =
        {
            // Bottom Face
            +0.5f,+0.5f,-0.5f, // 2
            -0.5f,-0.5f,-0.5f, // 0
            -0.5f,+0.5f,-0.5f, // 1

            -0.5f,-0.5f,-0.5f, // 0
            +0.5f,+0.5f,-0.5f, // 2
            +0.5f,-0.5f,-0.5f, // 3

            // Top
            -0.5f,+0.5f,+0.5f, // 5
            -0.5f,-0.5f,+0.5f, // 4
            +0.5f,+0.5f,+0.5f, // 6

            +0.5f,+0.5f,+0.5f, // 6
            -0.5f,-0.5f,+0.5f, // 4
            +0.5f,-0.5f,+0.5f, // 7

            // Front
            +0.5f,+0.5f,+0.5f, // 6
            +0.5f,-0.5f,+0.5f, // 7
            +0.5f,-0.5f,-0.5f, // 3

            +0.5f,+0.5f,+0.5f, // 6
            +0.5f,-0.5f,-0.5f, // 3
            +0.5f,+0.5f,-0.5f, // 2

            // Back
            -0.5f,-0.5f,+0.5f, // 4
            -0.5f,+0.5f,+0.5f, // 5
            -0.5f,+0.5f,-0.5f, // 1

            -0.5f,-0.5f,+0.5f, // 4
            -0.5f,+0.5f,-0.5f, // 1
            -0.5f,-0.5f,-0.5f, // 0

            // Right
            +0.5f,-0.5f,+0.5f, // 7
            -0.5f,-0.5f,+0.5f, // 4
            -0.5f,-0.5f,-0.5f, // 0

            +0.5f,-0.5f,+0.5f, // 7
            -0.5f,-0.5f,-0.5f, // 0
            +0.5f,-0.5f,-0.5f, // 3

            // Left
            -0.5f,+0.5f,+0.5f, // 5
            +0.5f,+0.5f,+0.5f, // 6
            +0.5f,+0.5f,-0.5f, // 2

            -0.5f,+0.5f,+0.5f, // 5
            +0.5f,+0.5f,-0.5f, // 2
            -0.5f,+0.5f,-0.5f, // 1
        };


    float tri_norms[N_TRI_VERT*3] =
    {
        // Bottom
        +0.0f,+0.0f,-1.0f,
        +0.0f,+0.0f,-1.0f,
        +0.0f,+0.0f,-1.0f,
        +0.0f,+0.0f,-1.0f,
        +0.0f,+0.0f,-1.0f,
        +0.0f,+0.0f,-1.0f,

        // Top
        +0.0f,+0.0f,+1.0f,
        +0.0f,+0.0f,+1.0f,
        +0.0f,+0.0f,+1.0f,
        +0.0f,+0.0f,+1.0f,
        +0.0f,+0.0f,+1.0f,
        +0.0f,+0.0f,+1.0f,

        // Front
        +1.0f,+0.0f,+0.0f,
        +1.0f,+0.0f,+0.0f,
        +1.0f,+0.0f,+0.0f,
        +1.0f,+0.0f,+0.0f,
        +1.0f,+0.0f,+0.0f,
        +1.0f,+0.0f,+0.0f,

        // Back
        -1.0f,+0.0f,+0.0f,
        -1.0f,+0.0f,+0.0f,
        -1.0f,+0.0f,+0.0f,
        -1.0f,+0.0f,+0.0f,
        -1.0f,+0.0f,+0.0f,
        -1.0f,+0.0f,+0.0f,

        // Right
        +0.0f,-1.0f,+0.0f,
        +0.0f,-1.0f,+0.0f,
        +0.0f,-1.0f,+0.0f,
        +0.0f,-1.0f,+0.0f,
        +0.0f,-1.0f,+0.0f,
        +0.0f,-1.0f,+0.0f,

        // Left
        +0.0f,+1.0f,+0.0f,
        +0.0f,+1.0f,+0.0f,
        +0.0f,+1.0f,+0.0f,
        +0.0f,+1.0f,+0.0f,
        +0.0f,+1.0f,+0.0f,
        +0.0f,+1.0f,+0.0f
    };

    // triangles in CCW when viewed from outside the box
    /*
      2,0,1,0,2,3,  // bottom face
      5,4,6,6,4,7,  // top face
      6,7,3,6,3,2,  // front face
      4,5,1,4,1,0,  // back face
      7,4,0,7,0,3,  // right face
      5,6,2,5,2,1   // left face
    */

    vertex_points = vx_resc_copyf(point_verts, NVERTS*3);
    line_indices = vx_resc_copyui(line_idxs, N_LINE_IDX*2);
    tri_points = vx_resc_copyf(tri_verts, N_TRI_VERT*3);
    tri_normals = vx_resc_copyf(tri_norms, N_TRI_VERT*3);

    // hold on to these references until vx_global_destroy()
    vx_resc_inc_ref(vertex_points);
    vx_resc_inc_ref(line_indices);
    vx_resc_inc_ref(tri_points);
    vx_resc_inc_ref(tri_normals);
}

// will run when the program vx_global_destroy() is called by user at end of program
static void vxo_box_destroy(void * ignored)
{
    vx_resc_dec_destroy(vertex_points);
    vx_resc_dec_destroy(line_indices);
    vx_resc_dec_destroy(tri_points);
    vx_resc_dec_destroy(tri_normals);
}

vx_object_t * _vxo_box_private(vx_style_t * style, ...)
{

    // Make sure the static geometry is initialized safely, correctly, and quickly
    if (vertex_points == NULL) {
        pthread_mutex_lock(&vx_convenience_mutex);
        if (vertex_points == NULL) {
            vxo_box_init();
            vx_global_register_destroy(vxo_box_destroy, NULL);
        }
        pthread_mutex_unlock(&vx_convenience_mutex);
    }

    vx_object_t * vc = vxo_chain_create();

    va_list va;
    va_start(va, style);

    for (vx_style_t * sty = style; sty != NULL; sty = va_arg(va, vx_style_t *)) {

        switch(sty->type) {
            case VXO_POINTS_STYLE:
                vxo_chain_add(vc, vxo_points(vertex_points, NVERTS, sty));
                break;
            case VXO_LINES_STYLE:
                vxo_chain_add(vc, vxo_lines_indexed(vertex_points, NVERTS, line_indices, GL_LINES, sty));
                break;
            case VXO_MESH_STYLE:
                // XXX always pass normals?
                vxo_chain_add(vc, vxo_mesh(tri_points, N_TRI_VERT,  tri_normals, GL_TRIANGLES, sty));
                break;
        }
    }
    va_end(va);

    return vc;
}
