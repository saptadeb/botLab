#include <stdarg.h>
#include <math.h>

#include "vxo_star.h"

#include "vx_codes.h"
#include "vx_global.h"


#include "vxo_chain.h"
#include "vxo_mesh.h"
#include "vxo_points.h"
#include "vxo_lines.h"
#include "vxo_mat.h"

// 5-pointed star with 10 vertices
//One at each corner of the inner pentagon,
//and one at each star point
#define NVERTS 10

static vx_resc_t * outline = NULL;
static vx_resc_t * fill_verts = NULL;
static vx_resc_t * fill_normals = NULL;

static void vxo_star_init()
{
    static float ratio = 0.5;

    float outline_f[NVERTS*2];          //LINE_LOOP
    for (int i = 0; i < NVERTS; i++) {
        float r = ((i & 1) == 0) ? 1 : ratio;
        float theta = i * 2*M_PI / NVERTS;

        outline_f[i*2 + 0] = r * cosf(theta);
        outline_f[i*2 + 1] = r * sinf(theta);
    }

    float fill_f[(NVERTS + 2) * 2];    //GL_TRIANGLE_FAN
    fill_f[0] = fill_f[1] = 0.0f;
    for (int i = 0; i <= NVERTS; i++) {
        float theta = i * 2*M_PI / NVERTS;
        float r = ((i & 1) == 0) ? 1 : ratio;

        fill_f[i*2 + 0 + 2] = r * cosf(theta);
        fill_f[i*2 + 1 + 2] = r * sinf(theta);
    }

    float fill_norm_f[(NVERTS + 2) * 3];
    for (int i = 0; i < NVERTS + 2; i++) {
        fill_norm_f[i*3 + 0] = 0.0f;
        fill_norm_f[i*3 + 1] = 0.0f;
        fill_norm_f[i*3 + 2] = 1.0f;
    }


    outline = vx_resc_copyf(outline_f, NVERTS*2);
    fill_verts = vx_resc_copyf(fill_f, (NVERTS+2)*2);
    fill_normals = vx_resc_copyf(fill_norm_f, (NVERTS+2)*3);

    vx_resc_inc_ref(outline); // hold on to these references until vx_global_destroy()
    vx_resc_inc_ref(fill_verts);
    vx_resc_inc_ref(fill_normals);
}

// will run when the program vx_global_destroy() is called by user at end of program
static void vxo_star_destroy(void * ignored)
{
    vx_resc_dec_destroy(outline);
    vx_resc_dec_destroy(fill_verts);
    vx_resc_dec_destroy(fill_normals);
}

vx_object_t * _vxo_star_private(vx_style_t * style, ...)
{
    // Make sure the static geometry is initialized safely, correctly, and quickly
    if (outline == NULL) {
        pthread_mutex_lock(&vx_convenience_mutex);
        if (outline == NULL) {
            vxo_star_init();
            vx_global_register_destroy(vxo_star_destroy, NULL);
        }
        pthread_mutex_unlock(&vx_convenience_mutex);
    }

    vx_object_t * vc = vxo_chain_create();
    va_list va;
    va_start(va, style);
    for (vx_style_t * sty = style; sty != NULL; sty = va_arg(va, vx_style_t *)) {

        switch(sty->type) {
            case VXO_POINTS_STYLE:
                vxo_chain_add(vc, vxo_points(outline, NVERTS, sty));
                break;
            case VXO_LINES_STYLE:
                vxo_chain_add(vc, vxo_lines(outline, NVERTS, GL_LINE_LOOP, sty));
                break;
            case VXO_MESH_STYLE:
                vxo_chain_add(vc, vxo_mesh(fill_verts, NVERTS + 2, fill_normals, GL_TRIANGLE_FAN, sty));
                break;
        }
    }
    va_end(va);

    return vc;
}
